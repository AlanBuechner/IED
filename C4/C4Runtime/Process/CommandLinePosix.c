// The program, arguments and environment a child process starts with (ProcessPosix.h). A bare program name is looked
// for in PATH as execvp does (an empty entry is the working folder; no PATH is /bin:/usr/bin), the first executable
// file found; nothing is added to the name, where Windows adds ".exe". argv[0] is the program as it was named, as a
// shell gives it. Environment names compare as Linux compares them: exactly.

#include "ProcessPosix.h"
#include "SystemErrorC.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char** environ;

static const char* const kDefaultSearchPath = "/bin:/usr/bin";
static const char* const kShell = "/bin/sh";
// A terminal's screen is VT text, as ConPTY's is on Windows: a child not told otherwise is told it is an xterm.
static const char* const kTerminalType = "TERM=xterm-256color";

char* c4_process_posix_absolute(const char* path)
{
	if (path[0] == '/') return strdup(path);
	char* folder = getcwd(NULL, 0);
	const size_t folderLength = folder ? strlen(folder) : 0;
	char* absolute = folder ? (char*)malloc(folderLength + strlen(path) + 2) : NULL;
	if (absolute)
	{
		memcpy(absolute, folder, folderLength);
		absolute[folderLength] = '/';
		strcpy(absolute + folderLength + 1, path);
	}
	free(folder);
	return absolute;
}

// --- the program -----------------------------------------------------------------------------------------------------

static bool IsProgram(const char* path)
{
	struct stat status;
	return stat(path, &status) == 0 && S_ISREG(status.st_mode) && access(path, X_OK) == 0;
}

// The first folder of `searchPath` (':'-separated) that holds a program named `name`: its path there (malloc'd), or
// null.
static char* Search(const char* name, const char* searchPath)
{
	const size_t nameLength = strlen(name);
	for (const char* folder = searchPath;;)
	{
		const char* end = strchr(folder, ':');
		const size_t folderLength = end ? (size_t)(end - folder) : strlen(folder);
		char* candidate = (char*)malloc(folderLength + nameLength + 3);
		if (!candidate) return NULL;
		if (folderLength) memcpy(candidate, folder, folderLength);
		else candidate[0] = '.';
		const size_t start = folderLength ? folderLength : 1;
		candidate[start] = '/';
		memcpy(candidate + start + 1, name, nameLength + 1);
		if (IsProgram(candidate)) return candidate;
		free(candidate);
		if (!end) return NULL;
		folder = end + 1;
	}
}

char* c4_process_posix_program(const char* program, char* error, int64_t errorCapacity)
{
	const char* searchPath = getenv("PATH");
	char* found = strchr(program, '/') ? strdup(program) : Search(program, searchPath ? searchPath : kDefaultSearchPath);
	char* path = found ? c4_process_posix_absolute(found) : NULL;
	free(found);
	if (path) return path;
	char message[512];
	snprintf(message, sizeof(message), "no program named \"%s\" was found", program);
	c4_copy_text(message, error, errorCapacity);
	return NULL;
}

char* c4_process_posix_shell(char* error, int64_t errorCapacity)
{
	char* shell = IsProgram(kShell) ? strdup(kShell) : NULL;
	if (!shell) c4_copy_text("the system's shell (/bin/sh) was not found", error, errorCapacity);
	return shell;
}

// --- the arguments ---------------------------------------------------------------------------------------------------

char** c4_process_posix_arguments(const c4_process_options* start)
{
	const bool shell = start->shellCommand && *start->shellCommand;
	const int32_t given = start->argumentCount > 0 ? start->argumentCount : 0;
	const int32_t count = shell ? 3 : 1 + given;
	char** arguments = (char**)malloc(((size_t)count + 1) * sizeof(char*));
	if (!arguments) return NULL;
	if (shell)
	{
		arguments[0] = "sh";
		arguments[1] = "-c";
		arguments[2] = (char*)start->shellCommand;
	}
	else
	{
		arguments[0] = (char*)start->program;
		for (int32_t i = 0; i < given; ++i) arguments[i + 1] = (char*)(start->arguments[i] ? start->arguments[i] : "");
	}
	arguments[count] = NULL;
	return arguments;
}

// --- the environment -------------------------------------------------------------------------------------------------

// "NAME=value" entries, not owned, and the null that ends them once Add(entries, NULL) has put it.
typedef struct Entries { char** items; size_t count; size_t capacity; } Entries;

static bool Add(Entries* entries, char* entry)
{
	if (entries->count == entries->capacity)
	{
		const size_t capacity = entries->capacity ? entries->capacity * 2 : 64;
		char** grown = (char**)realloc(entries->items, capacity * sizeof(char*));
		if (!grown) return false;
		entries->items = grown;
		entries->capacity = capacity;
	}
	entries->items[entries->count++] = entry;
	return true;
}

static size_t NameLength(const char* entry)
{
	const char* equals = strchr(entry, '=');
	return equals ? (size_t)(equals - entry) : strlen(entry);
}

static bool SameName(const char* a, const char* b)
{
	const size_t length = NameLength(a);
	return length == NameLength(b) && memcmp(a, b, length) == 0;
}

static bool HasName(const Entries* entries, const char* name)
{
	for (size_t i = 0; i < entries->count; ++i)
		if (SameName(entries->items[i], name)) return true;
	return false;
}

// This process's environment as it is now (another thread's setenv meanwhile is not safe: glibc's is unlocked).
static bool AddInherited(Entries* entries)
{
	for (char** entry = environ; entry && *entry; ++entry)
		if (!Add(entries, *entry)) return false;
	return true;
}

// Sets or (a bare name) removes a variable, every entry of that name.
static bool Apply(Entries* entries, char* change)
{
	size_t kept = 0;
	for (size_t i = 0; i < entries->count; ++i)
		if (!SameName(entries->items[i], change)) entries->items[kept++] = entries->items[i];
	entries->count = kept;
	return change[NameLength(change)] == 0 || Add(entries, change);
}

// False with `*invalid` for an entry that names nothing ("" or "=value").
static bool ApplyChanges(Entries* entries, const c4_process_options* start, bool* invalid)
{
	for (int32_t i = 0; i < start->environmentCount; ++i)
	{
		char* change = (char*)(start->environment[i] ? start->environment[i] : "");
		*invalid = change[0] == 0 || change[0] == '=';
		if (*invalid || !Apply(entries, change)) return false;
	}
	return true;
}

char** c4_process_posix_environment(const c4_process_options* start, bool* failed)
{
	*failed = false;
	Entries entries = { 0 };
	bool ok = (!start->inheritEnvironment || AddInherited(&entries)) && ApplyChanges(&entries, start, failed);
	if (ok && start->terminal && !HasName(&entries, kTerminalType)) ok = Add(&entries, (char*)kTerminalType);
	if (ok) ok = Add(&entries, NULL);
	if (ok) return entries.items;
	free(entries.items);
	return NULL;
}
