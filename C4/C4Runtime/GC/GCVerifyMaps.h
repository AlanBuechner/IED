#pragma once
// C4_GC_VERIFY_MAPS=1 (R16, BUG-497): pointer maps checked against what the program keeps in its memory. Once a
// collection has marked, every word a map says holds no pointer - of a live precise object, of a typed scan region's
// elements, of a mapped global or thread_local root - is read, and one that names an object of the heap is reported: a
// pointer the map misses (a number holding one, or a wrong map). A target left unmarked is one the sweep frees while
// the word still names it. Reports are kept during the stop and printed after it: printing while the other threads
// are stopped can wait forever on a lock one of them holds.

#include <stdint.h>

#define GC_MAP_REPORTS 64u

typedef enum GCMapHolder
{
	GC_MAP_OBJECT,            // a word of a precise object
	GC_MAP_REGION,            // a word of an element of a typed scan region
	GC_MAP_GLOBAL,            // a word of a mapped global root
	GC_MAP_THREAD_LOCAL       // a word of a mapped thread_local root
} GCMapHolder;

typedef struct GCMapReport
{
	GCMapHolder holder;
	const void* key;          // the map (objects, regions, thread_locals) or root range (globals) the word is read by
	const char* type;         // the type's name when known, else NULL
	const void* where;        // a global's address, else the object's or region's start when first seen
	uint32_t word;            // the word's index in its object, element or range
	int unmarked;             // a target was not marked: the sweep frees it
	uintptr_t value;          // the word's value when first seen
} GCMapReport;

typedef struct GCMapReports
{
	int on;
	const char* log;          // C4_GC_VERIFY_MAPS=<path>: reports are appended there (servers whose stderr goes nowhere)
	GCMapReport found[GC_MAP_REPORTS];
	uint32_t count;           // distinct words kept (at most GC_MAP_REPORTS)
	uint32_t printed;         // ... of which printed
	uint64_t misses;          // every miss seen, repeats included
	uint64_t checks;          // collections checked
	int announced;            // the first check was said (a process killed at its end prints no summary)
} GCMapReports;

struct GCState;
// Checks the words the maps leave out, as above. The other threads are stopped and marking is done.
void gc_verify_maps(struct GCState* gc);
// The reports not printed yet, copied out (at most `capacity`), marked printed. Under the lock.
uint32_t gc_verify_maps_take(GCMapReports* reports, GCMapReport* out, uint32_t capacity);
// One report as a line on stderr, or appended to `log`. Without the lock, the threads running.
void gc_verify_maps_print(const GCMapReport* report, const char* log);
// That the check runs in this process (once, at its first checked collection).
void gc_verify_maps_announce(const char* log);
// The summary at the program's end: how many words named an object where a map says none is.
void gc_verify_maps_summary(uint64_t misses, const char* log);
