#pragma once
// What the Linux platform files share: the process's mappings as /proc/self/maps lists them (VirtualMemoryLinux.c
// queries memory with them) and the ELF files behind them, whose symbols and line tables name code addresses
// (CrashReportLinux.c, SourceLinesLinux.c). All of it but the line index is async-signal-safe - system calls and the
// caller's memory, no allocation, no lock - so a fault's report can use it.

#include <elf.h>
#include <stdbool.h>
#include <stdint.h>

enum { kLinuxPathBytes = 4096 };

// One line of /proc/self/maps.
typedef struct c4_linux_mapping
{
	uintptr_t start;
	uintptr_t end;
	uint64_t offset;      // of `start` in the file
	uint64_t inode;       // the file's (0 for memory that is no file's)
	char rights[5];       // "r-xp": read, write, execute, private or shared
	const char* path;     // "" for anonymous memory, "[stack]", "[vdso]"; " (deleted)" follows a file deleted since
} c4_linux_mapping;

// Called for each mapping in address order; false stops the walk. `mapping->path` lives until it returns.
typedef bool (*c4_linux_mapping_visitor)(const c4_linux_mapping* mapping, void* state);

// Walks /proc/self/maps. False when it cannot be read.
bool c4_linux_each_mapping(c4_linux_mapping_visitor visit, void* state);

// The mapping holding `address`, its path copied into `path` (kLinuxPathBytes, " (deleted)" cut off). False when no
// mapping holds it.
bool c4_linux_mapping_of(uintptr_t address, c4_linux_mapping* mapping, char* path);

// An ELF file mapped for reading.
typedef struct c4_linux_elf
{
	const uint8_t* bytes;
	uint64_t size;
	const Elf64_Ehdr* header;
	const Elf64_Shdr* sections;   // null when the file has none
	uint32_t sectionCount;
} c4_linux_elf;

// Maps the file at `path` when it is a 64-bit little-endian ELF file and, when `inode` is not 0, the very file a mapping
// came from (a program rebuilt while it runs has a new file at its path). False otherwise.
bool c4_linux_elf_open(const char* path, uint64_t inode, c4_linux_elf* elf);
void c4_linux_elf_close(c4_linux_elf* elf);

// What to add to an address in the file (a symbol's value, a line table's) for where `mapping` put it.
bool c4_linux_elf_load_bias(const c4_linux_elf* elf, const c4_linux_mapping* mapping, uintptr_t* bias);

// The section named `name`, uncompressed; false when the file has none.
bool c4_linux_elf_section(const c4_linux_elf* elf, const char* name, const uint8_t** bytes, uint64_t* size);

// The function holding the file address `address`, from the symbol table, else the dynamic one: its name, with its
// start in `*start`. Null when no symbol holds it (a stripped file, a static function a stripped library hides).
const char* c4_linux_elf_function(const c4_linux_elf* elf, uint64_t address, uint64_t* start);

// The source line of a file address, from a .debug_line section (DWARF 2 to 5). The file is `directory`/`name`
// (`directory` is "" when `name` is absolute or the table has none); the strings live in the mapped file.
typedef struct c4_linux_line
{
	uint64_t address;        // asked
	const char* directory;   // found: null until it is
	const char* name;
	int32_t line;
} c4_linux_line;

// Answers every one of `lines` (sorted by address) that the line tables hold, in one pass over them.
void c4_linux_elf_lines(const c4_linux_elf* elf, c4_linux_line* lines, int32_t count);

// An answered line's file as one path into `out`, joined with the directory's own separator (a program built on
// Windows names Windows folders): its length, or -1 when it does not fit in `capacity` with its terminator.
int32_t c4_linux_line_path(const c4_linux_line* line, char* out, int32_t capacity);

// A file's line tables indexed for many lookups (SourceLinesLinux.c): every few rows, a row and where its program goes
// on, so a lookup runs a few rows instead of the tables. Built with malloc: not for a signal handler.
typedef struct c4_linux_line_index
{
	struct c4_linux_line_checkpoint* checkpoints;   // by address
	int64_t count;
} c4_linux_line_index;

// False when the file has no line tables or memory ran out.
bool c4_linux_line_index_build(const c4_linux_elf* elf, c4_linux_line_index* index);
void c4_linux_line_index_free(c4_linux_line_index* index);
// Answers `line` (its address set) as c4_linux_elf_lines does; false when the tables do not hold it.
bool c4_linux_line_index_find(const c4_linux_elf* elf, const c4_linux_line_index* index, c4_linux_line* line);
