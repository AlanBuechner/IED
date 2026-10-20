// The program, command line and environment a child process starts with (ProcessWin32.h).

#include "ProcessWin32.h"
#include "PathRuntimeC.h"
#include "SystemErrorC.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// A growable UTF-16 buffer.
typedef struct WideText { wchar_t* text; size_t length; size_t capacity; } WideText;

static bool Reserve(WideText* w, size_t more)
{
	if (w->length + more + 1 <= w->capacity) return true;
	size_t capacity = w->capacity ? w->capacity * 2 : 256;
	while (capacity < w->length + more + 1) capacity *= 2;
	wchar_t* grown = (wchar_t*)realloc(w->text, capacity * sizeof(wchar_t));
	if (!grown) return false;
	w->text = grown;
	w->capacity = capacity;
	return true;
}

static bool Put(WideText* w, wchar_t c)
{
	if (!Reserve(w, 1)) return false;
	w->text[w->length++] = c;
	w->text[w->length] = 0;
	return true;
}

static bool PutRepeated(WideText* w, wchar_t c, size_t count)
{
	for (size_t i = 0; i < count; ++i)
		if (!Put(w, c)) return false;
	return true;
}

// --- the program -----------------------------------------------------------------------------------------------------

static bool EndsWith(const wchar_t* text, const wchar_t* suffix)
{
	const size_t length = wcslen(text);
	const size_t suffixLength = wcslen(suffix);
	return length >= suffixLength && _wcsicmp(text + length - suffixLength, suffix) == 0;
}

static wchar_t* Search(const wchar_t* name)
{
	const DWORD needed = SearchPathW(NULL, name, L".exe", 0, NULL, NULL);
	if (needed == 0) return NULL;
	wchar_t* found = (wchar_t*)malloc(needed * sizeof(wchar_t));
	const DWORD length = found ? SearchPathW(NULL, name, L".exe", needed, found, NULL) : 0;
	if (length > 0 && length < needed) return found;
	free(found);
	return NULL;
}

wchar_t* c4_process_win32_program(const char* program, char* error, int64_t errorCapacity)
{
	wchar_t* wide = c4_utf8_to_wide(program);
	if (!wide)
	{
		c4_copy_text("the program's name is not UTF-8", error, errorCapacity);
		return NULL;
	}
	wchar_t* path = wcspbrk(wide, L"\\/") ? wide : Search(wide);
	if (path != wide) free(wide);
	if (!path)
	{
		char message[512];
		snprintf(message, sizeof(message), "no program named \"%s\" was found", program);
		c4_copy_text(message, error, errorCapacity);
		return NULL;
	}
	if (!EndsWith(path, L".bat") && !EndsWith(path, L".cmd")) return path;
	free(path);
	c4_copy_text("a batch file runs through \"cmd.exe /c\", which reads its arguments its own way", error, errorCapacity);
	return NULL;
}

// --- the command line ------------------------------------------------------------------------------------------------

// One argument as the Microsoft C runtime and CommandLineToArgvW read it back: quoted when it is empty or holds a
// space, tab or quote, with the backslashes before a quote doubled.
static bool PutArgument(WideText* line, const wchar_t* argument)
{
	if (*argument && !wcspbrk(argument, L" \t\n\v\""))
	{
		for (const wchar_t* c = argument; *c; ++c)
			if (!Put(line, *c)) return false;
		return true;
	}
	if (!Put(line, L'"')) return false;
	for (const wchar_t* c = argument;; ++c)
	{
		size_t backslashes = 0;
		while (*c == L'\\') { ++backslashes; ++c; }
		if (!*c) return PutRepeated(line, L'\\', backslashes * 2) && Put(line, L'"');
		const bool quote = *c == L'"';
		if (!PutRepeated(line, L'\\', quote ? backslashes * 2 + 1 : backslashes) || !Put(line, *c)) return false;
	}
}

static bool PutUtf8Argument(WideText* line, const char* argument)
{
	wchar_t* wide = c4_utf8_to_wide(argument ? argument : "");
	const bool put = wide && PutArgument(line, wide);
	free(wide);
	return put;
}

wchar_t* c4_process_win32_command_line(const c4_process_options* start, const wchar_t* program)
{
	WideText line = { 0 };
	bool ok = PutArgument(&line, program);
	for (int32_t i = 0; ok && i < start->argumentCount; ++i) ok = Put(&line, L' ') && PutUtf8Argument(&line, start->arguments[i]);
	if (ok) return line.text;
	free(line.text);
	return NULL;
}

// --- the environment -------------------------------------------------------------------------------------------------

// "NAME=value" entries, each malloc'd.
typedef struct Entries { wchar_t** items; size_t count; size_t capacity; } Entries;

static bool Add(Entries* entries, wchar_t* entry)
{
	if (entries->count == entries->capacity)
	{
		const size_t capacity = entries->capacity ? entries->capacity * 2 : 64;
		wchar_t** grown = (wchar_t**)realloc(entries->items, capacity * sizeof(wchar_t*));
		if (!grown) return false;
		entries->items = grown;
		entries->capacity = capacity;
	}
	entries->items[entries->count++] = entry;
	return true;
}

static void FreeEntries(Entries* entries)
{
	for (size_t i = 0; i < entries->count; ++i) free(entries->items[i]);
	free(entries->items);
}

// A name may start with '=': the system keeps each drive's folder as "=C:=C:\work".
static size_t NameLength(const wchar_t* entry)
{
	const wchar_t* equals = wcschr(entry + (entry[0] == L'=' ? 1 : 0), L'=');
	return equals ? (size_t)(equals - entry) : wcslen(entry);
}

// Names compare as the system compares them: ordinal, ignoring case.
static int CompareNames(const wchar_t* a, const wchar_t* b)
{
	return CompareStringOrdinal(a, (int)NameLength(a), b, (int)NameLength(b), TRUE) - CSTR_EQUAL;
}

static int CompareEntries(const void* a, const void* b) { return CompareNames(*(wchar_t* const*)a, *(wchar_t* const*)b); }

static bool AddInherited(Entries* entries)
{
	wchar_t* block = GetEnvironmentStringsW();
	if (!block) return false;
	bool ok = true;
	for (const wchar_t* entry = block; ok && *entry; entry += wcslen(entry) + 1)
	{
		wchar_t* copy = _wcsdup(entry);
		ok = copy && Add(entries, copy);
		if (!ok) free(copy);
	}
	FreeEnvironmentStringsW(block);
	return ok;
}

// Sets or (a bare name) removes a variable; takes `change`.
static bool Apply(Entries* entries, wchar_t* change)
{
	const bool removes = change[NameLength(change)] == 0;
	for (size_t i = 0; i < entries->count; ++i)
	{
		if (CompareNames(entries->items[i], change) != 0) continue;
		free(entries->items[i]);
		if (!removes)
		{
			entries->items[i] = change;
			return true;
		}
		entries->items[i] = entries->items[--entries->count];
		break;
	}
	if (!removes) return Add(entries, change);
	free(change);
	return true;
}

static bool ApplyChanges(Entries* entries, const c4_process_options* start)
{
	for (int32_t i = 0; i < start->environmentCount; ++i)
	{
		wchar_t* change = c4_utf8_to_wide(start->environment[i] ? start->environment[i] : "");
		if (!change || change[0] == 0 || change[0] == L'=' || !Apply(entries, change))
		{
			free(change);
			return false;
		}
	}
	return true;
}

// "NAME=value\0NAME=value\0\0", sorted by name as CreateProcess asks.
static wchar_t* Block(Entries* entries)
{
	qsort(entries->items, entries->count, sizeof(wchar_t*), CompareEntries);
	WideText block = { 0 };
	for (size_t i = 0; i < entries->count; ++i)
	{
		const size_t length = wcslen(entries->items[i]);
		if (!Reserve(&block, length + 1)) { free(block.text); return NULL; }
		memcpy(block.text + block.length, entries->items[i], (length + 1) * sizeof(wchar_t));
		block.length += length + 1;
	}
	if (!Reserve(&block, 2)) { free(block.text); return NULL; }
	block.text[block.length] = 0;
	block.text[block.length + 1] = 0;
	return block.text;
}

wchar_t* c4_process_win32_environment(const c4_process_options* start, bool* failed)
{
	*failed = false;
	if (start->inheritEnvironment && start->environmentCount == 0) return NULL;
	Entries entries = { 0 };
	wchar_t* block = NULL;
	if ((!start->inheritEnvironment || AddInherited(&entries)) && ApplyChanges(&entries, start)) block = Block(&entries);
	FreeEntries(&entries);
	*failed = block == NULL;
	return block;
}
