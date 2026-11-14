#pragma once
// The collector's private heaps on Linux (GCPlatformPosix.c): its bookkeeping and its marking scratch never come from
// malloc, whose lock a thread the collector stopped may hold. Blocks of power-of-two size classes carved from mmap'd
// chunks, a mapping of their own past the largest class.

#include <stddef.h>
#include <stdint.h>

enum { GC_POSIX_HEAP_CLASSES = 12 }; // 16 bytes to 32 KiB

typedef struct GCPosixHeap
{
	void* freeLists[GC_POSIX_HEAP_CLASSES];
	uint8_t* next; // the unused part of the newest chunk
	uint8_t* end;
} GCPosixHeap;

// `size` zeroed bytes, 16-byte aligned; NULL when the OS refuses. The caller serializes calls on one heap.
void* gc_posix_heap_alloc(GCPosixHeap* heap, size_t size);
// `p` grown or shrunk to `size` (moved when its block is too small), what it held kept and the new bytes zeroed.
void* gc_posix_heap_realloc(GCPosixHeap* heap, void* p, size_t size);
void gc_posix_heap_free(GCPosixHeap* heap, void* p);
