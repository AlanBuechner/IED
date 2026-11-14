// The process's mappings and the ELF files behind them (ModulesLinux.h): /proc/self/maps read a buffer at a time on
// the caller's stack, files mapped with mmap. Async-signal-safe throughout.

#include "ModulesLinux.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// A line of /proc/self/maps is its fields (under 100 bytes) and a path of at most kLinuxPathBytes.
enum { kMapsBufferBytes = 2 * kLinuxPathBytes };

static const char kDeletedSuffix[] = " (deleted)";

// --- /proc/self/maps ---------------------------------------------------------------------------------------

static int HexDigit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static uint64_t ParseNumber(const char** cursor, uint32_t base)
{
	uint64_t value = 0;
	for (int digit; (digit = HexDigit(**cursor)) >= 0 && (uint32_t)digit < base; ++*cursor) value = value * base + (uint64_t)digit;
	return value;
}

static const char* SkipSpaces(const char* cursor)
{
	while (*cursor == ' ') ++cursor;
	return cursor;
}

static const char* SkipField(const char* cursor)
{
	while (*cursor && *cursor != ' ') ++cursor;
	return SkipSpaces(cursor);
}

// "start-end rights offset device inode   path".
static bool ParseMapping(const char* line, c4_linux_mapping* mapping)
{
	const char* cursor = line;
	mapping->start = (uintptr_t)ParseNumber(&cursor, 16);
	if (*cursor++ != '-') return false;
	mapping->end = (uintptr_t)ParseNumber(&cursor, 16);
	if (*cursor++ != ' ' || strlen(cursor) < 4) return false;
	memcpy(mapping->rights, cursor, 4);
	mapping->rights[4] = 0;
	cursor = SkipSpaces(cursor + 4);
	mapping->offset = ParseNumber(&cursor, 16);
	cursor = SkipField(SkipSpaces(cursor));
	mapping->inode = ParseNumber(&cursor, 10);
	mapping->path = SkipSpaces(cursor);
	return mapping->start < mapping->end;
}

// The whole lines in `buffer[0, held)`, each visited; the bytes of the line not yet whole are moved to the front.
// False once the visitor stops.
static bool VisitLines(char* buffer, size_t* held, c4_linux_mapping_visitor visit, void* state)
{
	size_t lineStart = 0;
	for (size_t i = 0; i < *held; ++i)
	{
		if (buffer[i] != '\n') continue;
		buffer[i] = 0;
		c4_linux_mapping mapping;
		if (ParseMapping(buffer + lineStart, &mapping) && !visit(&mapping, state)) return false;
		lineStart = i + 1;
	}
	memmove(buffer, buffer + lineStart, *held - lineStart);
	*held -= lineStart;
	return true;
}

bool c4_linux_each_mapping(c4_linux_mapping_visitor visit, void* state)
{
	const int file = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
	if (file < 0) return false;
	char buffer[kMapsBufferBytes];
	size_t held = 0;
	bool going = true;
	while (going)
	{
		if (held == sizeof(buffer)) held = 0; // a line longer than any the kernel writes: dropped
		const ssize_t got = read(file, buffer + held, sizeof(buffer) - held);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) break;
		held += (size_t)got;
		going = VisitLines(buffer, &held, visit, state);
	}
	close(file);
	return true;
}

typedef struct Holding
{
	uintptr_t address;
	c4_linux_mapping* mapping;
	char* path;
	bool found;
} Holding;

static void CopyPath(const char* path, char* out)
{
	size_t length = strlen(path);
	if (length >= kLinuxPathBytes) length = kLinuxPathBytes - 1;
	const size_t suffix = sizeof(kDeletedSuffix) - 1;
	if (length > suffix && memcmp(path + length - suffix, kDeletedSuffix, suffix) == 0) length -= suffix;
	memcpy(out, path, length);
	out[length] = 0;
}

static bool FindHolding(const c4_linux_mapping* mapping, void* state)
{
	Holding* holding = (Holding*)state;
	if (holding->address < mapping->start) return false;
	if (holding->address >= mapping->end) return true;
	*holding->mapping = *mapping;
	CopyPath(mapping->path, holding->path);
	holding->mapping->path = holding->path;
	holding->found = true;
	return false;
}

bool c4_linux_mapping_of(uintptr_t address, c4_linux_mapping* mapping, char* path)
{
	Holding holding = { address, mapping, path, false };
	return c4_linux_each_mapping(FindHolding, &holding) && holding.found;
}

// --- ELF files ---------------------------------------------------------------------------------------------

static bool Holds(uint64_t size, uint64_t offset, uint64_t length) { return offset <= size && length <= size - offset; }

static bool IsElf64(const uint8_t* bytes, uint64_t size)
{
	if (size < sizeof(Elf64_Ehdr) || memcmp(bytes, ELFMAG, SELFMAG) != 0) return false;
	return bytes[EI_CLASS] == ELFCLASS64 && bytes[EI_DATA] == ELFDATA2LSB;
}

static void FindSections(c4_linux_elf* elf)
{
	const Elf64_Ehdr* header = elf->header;
	const uint64_t bytes = (uint64_t)header->e_shnum * sizeof(Elf64_Shdr);
	if (header->e_shnum == 0 || header->e_shentsize != sizeof(Elf64_Shdr) || !Holds(elf->size, header->e_shoff, bytes)) return;
	if (header->e_shstrndx >= header->e_shnum) return;
	elf->sections = (const Elf64_Shdr*)(elf->bytes + header->e_shoff);
	elf->sectionCount = header->e_shnum;
}

bool c4_linux_elf_open(const char* path, uint64_t inode, c4_linux_elf* elf)
{
	memset(elf, 0, sizeof(*elf));
	const int file = open(path, O_RDONLY | O_CLOEXEC);
	if (file < 0) return false;
	struct stat info;
	const bool same = fstat(file, &info) == 0 && S_ISREG(info.st_mode) && (inode == 0 || (uint64_t)info.st_ino == inode);
	void* bytes = same && info.st_size > 0 ? mmap(NULL, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, file, 0) : MAP_FAILED;
	close(file);
	if (bytes == MAP_FAILED) return false;
	elf->bytes = (const uint8_t*)bytes;
	elf->size = (uint64_t)info.st_size;
	if (!IsElf64(elf->bytes, elf->size))
	{
		c4_linux_elf_close(elf);
		return false;
	}
	elf->header = (const Elf64_Ehdr*)elf->bytes;
	FindSections(elf);
	return true;
}

void c4_linux_elf_close(c4_linux_elf* elf)
{
	if (elf->bytes) munmap((void*)elf->bytes, (size_t)elf->size);
	memset(elf, 0, sizeof(*elf));
}

// Whether `mapping` is (a part of) loadable segment `segment`: a segment is mapped from the page holding its first
// byte, so two segments sharing a page of the file both start at it; the executable one is told apart by its rights.
static bool MapsSegment(const c4_linux_mapping* mapping, const Elf64_Phdr* segment)
{
	const uint64_t page = (uint64_t)getpagesize();
	const uint64_t first = segment->p_offset & ~(page - 1);
	const bool executable = mapping->rights[2] == 'x';
	if (segment->p_type != PT_LOAD || executable != ((segment->p_flags & PF_X) != 0)) return false;
	return mapping->offset >= first && mapping->offset < segment->p_offset + segment->p_filesz;
}

bool c4_linux_elf_load_bias(const c4_linux_elf* elf, const c4_linux_mapping* mapping, uintptr_t* bias)
{
	const Elf64_Ehdr* header = elf->header;
	const uint64_t bytes = (uint64_t)header->e_phnum * sizeof(Elf64_Phdr);
	if (header->e_phentsize != sizeof(Elf64_Phdr) || !Holds(elf->size, header->e_phoff, bytes)) return false;
	const Elf64_Phdr* segments = (const Elf64_Phdr*)(elf->bytes + header->e_phoff);
	for (uint32_t i = 0; i < header->e_phnum; ++i)
	{
		const Elf64_Phdr* segment = &segments[i];
		if (!MapsSegment(mapping, segment)) continue;
		*bias = mapping->start - (uintptr_t)(segment->p_vaddr - segment->p_offset + mapping->offset);
		return true;
	}
	return false;
}

static const char* SectionName(const c4_linux_elf* elf, const Elf64_Shdr* section)
{
	const Elf64_Shdr* names = &elf->sections[elf->header->e_shstrndx];
	if (!Holds(elf->size, names->sh_offset, names->sh_size) || section->sh_name >= names->sh_size) return "";
	const char* name = (const char*)(elf->bytes + names->sh_offset + section->sh_name);
	return memchr(name, 0, names->sh_size - section->sh_name) ? name : "";
}

bool c4_linux_elf_section(const c4_linux_elf* elf, const char* name, const uint8_t** bytes, uint64_t* size)
{
	for (uint32_t i = 0; i < elf->sectionCount; ++i)
	{
		const Elf64_Shdr* section = &elf->sections[i];
		if (section->sh_type == SHT_NOBITS || (section->sh_flags & SHF_COMPRESSED) || strcmp(SectionName(elf, section), name) != 0) continue;
		if (!Holds(elf->size, section->sh_offset, section->sh_size)) return false;
		*bytes = elf->bytes + section->sh_offset;
		*size = section->sh_size;
		return true;
	}
	return false;
}

// A symbol table and the strings it names.
typedef struct SymbolTable
{
	const Elf64_Sym* symbols;
	uint64_t count;
	const char* names;
	uint64_t namesSize;
} SymbolTable;

static bool FindSymbolTable(const c4_linux_elf* elf, uint32_t type, SymbolTable* table)
{
	for (uint32_t i = 0; i < elf->sectionCount; ++i)
	{
		const Elf64_Shdr* section = &elf->sections[i];
		if (section->sh_type != type || section->sh_link >= elf->sectionCount) continue;
		const Elf64_Shdr* names = &elf->sections[section->sh_link];
		if (!Holds(elf->size, section->sh_offset, section->sh_size) || !Holds(elf->size, names->sh_offset, names->sh_size)) return false;
		table->symbols = (const Elf64_Sym*)(elf->bytes + section->sh_offset);
		table->count = section->sh_size / sizeof(Elf64_Sym);
		table->names = (const char*)(elf->bytes + names->sh_offset);
		table->namesSize = names->sh_size;
		return true;
	}
	return false;
}

static bool IsFunctionHolding(const Elf64_Sym* symbol, uint64_t address)
{
	const unsigned type = ELF64_ST_TYPE(symbol->st_info);
	const bool code = type == STT_FUNC || type == STT_GNU_IFUNC;
	return code && symbol->st_shndx != SHN_UNDEF && address >= symbol->st_value && address - symbol->st_value < symbol->st_size;
}

static const char* FunctionIn(const SymbolTable* table, uint64_t address, uint64_t* start)
{
	for (uint64_t i = 0; i < table->count; ++i)
	{
		const Elf64_Sym* symbol = &table->symbols[i];
		if (!IsFunctionHolding(symbol, address) || symbol->st_name >= table->namesSize) continue;
		const char* name = table->names + symbol->st_name;
		if (!memchr(name, 0, table->namesSize - symbol->st_name) || !*name) continue;
		*start = symbol->st_value;
		return name;
	}
	return NULL;
}

const char* c4_linux_elf_function(const c4_linux_elf* elf, uint64_t address, uint64_t* start)
{
	SymbolTable table;
	const char* name = FindSymbolTable(elf, SHT_SYMTAB, &table) ? FunctionIn(&table, address, start) : NULL;
	if (!name && FindSymbolTable(elf, SHT_DYNSYM, &table)) name = FunctionIn(&table, address, start);
	return name;
}
