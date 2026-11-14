// Source lines of code addresses for Bedrock's Platform::SourceLineOf (PlatformRuntimeC.h) on Linux, from the DWARF
// line tables of the ELF file the address is in (a program built with -g carries them). The file is the one the
// process mapped (/proc/self/maps; one rebuilt since has another inode and is not read). The first question about a
// file maps it and indexes its tables; files are kept for the next questions. Questions go one at a time.

#include "PlatformRuntimeC.h"
#include "ModulesLinux.h"
#include <pthread.h>
#include <string.h>

// Files kept indexed; one more replaces the oldest.
enum { kKeptModules = 16 };

typedef struct Module
{
	char path[kLinuxPathBytes];
	uint64_t inode;
	c4_linux_elf elf;
	c4_linux_line_index index;   // empty when the file has no line tables
} Module;

typedef struct SourceLines
{
	pthread_mutex_t lock;
	Module modules[kKeptModules];
	int32_t count;
	int32_t oldest;
} SourceLines;

static SourceLines g_lines = { .lock = PTHREAD_MUTEX_INITIALIZER };

static Module* Kept(SourceLines* lines, const char* path, uint64_t inode)
{
	for (int32_t i = 0; i < lines->count; ++i)
		if (lines->modules[i].inode == inode && strcmp(lines->modules[i].path, path) == 0) return &lines->modules[i];
	return NULL;
}

static void Drop(Module* module)
{
	c4_linux_line_index_free(&module->index);
	c4_linux_elf_close(&module->elf);
}

// A free place, or the oldest one's.
static Module* Place(SourceLines* lines)
{
	if (lines->count < kKeptModules) return &lines->modules[lines->count++];
	Module* oldest = &lines->modules[lines->oldest];
	lines->oldest = (lines->oldest + 1) % kKeptModules;
	Drop(oldest);
	return oldest;
}

static Module* Keep(SourceLines* lines, const char* path, uint64_t inode)
{
	c4_linux_elf elf;
	if (!c4_linux_elf_open(path, inode, &elf)) return NULL;
	Module* module = Place(lines);
	strcpy(module->path, path);
	module->inode = inode;
	module->elf = elf;
	c4_linux_line_index_build(&module->elf, &module->index);
	return module;
}

static int32_t LineOf(SourceLines* lines, uintptr_t code, char* file, int32_t capacity, int32_t* line)
{
	c4_linux_mapping mapping;
	char path[kLinuxPathBytes];
	if (!c4_linux_mapping_of(code, &mapping, path) || mapping.inode == 0) return 0;
	Module* module = Kept(lines, path, mapping.inode);
	if (!module) module = Keep(lines, path, mapping.inode);
	uintptr_t bias = 0;
	if (!module || !module->index.count || !c4_linux_elf_load_bias(&module->elf, &mapping, &bias)) return 0;
	c4_linux_line found = { (uint64_t)(code - bias), NULL, NULL, 0 };
	if (!c4_linux_line_index_find(&module->elf, &module->index, &found) || found.line <= 0) return 0;
	const int32_t length = c4_linux_line_path(&found, file, capacity);
	if (length <= 0) return 0;
	*line = found.line;
	return length;
}

int32_t c4_source_line_of(const void* code, char* file, int32_t capacity, int32_t* line)
{
	*line = 0;
	if (!code || !file || capacity <= 0) return 0;
	pthread_mutex_lock(&g_lines.lock);
	const int32_t length = LineOf(&g_lines, (uintptr_t)code, file, capacity, line);
	pthread_mutex_unlock(&g_lines.lock);
	return length;
}
