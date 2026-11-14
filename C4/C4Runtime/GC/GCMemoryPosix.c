#include "GC/GCMemoryPosix.h"
#include <string.h>
#include <sys/mman.h>

enum
{
	kHeader = 16,                   // a block's header; keeps the bytes it gives 16-byte aligned
	kSmallest = 16,
	kChunk = 1 << 20,
	kLarge = 0xFFFFFFFFu,           // the class of a block that is a mapping of its own
};

typedef struct BlockHeader
{
	uint64_t requested; // what the caller asked for: realloc zeroes what lies past it
	uint32_t sizeClass;
	uint32_t unused;
} BlockHeader;

static BlockHeader* header_of(void* p) { return (BlockHeader*)((uint8_t*)p - kHeader); }

static size_t class_size(uint32_t sizeClass) { return (size_t)kSmallest << sizeClass; }

// The smallest class that holds `size`, or kLarge.
static uint32_t class_of(size_t size)
{
	for (uint32_t c = 0; c < GC_POSIX_HEAP_CLASSES; ++c)
		if (size <= class_size(c)) return c;
	return kLarge;
}

static size_t page_round(size_t size) { return (size + 4095) & ~(size_t)4095; }

static void* map_zeroed(size_t size)
{
	void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? NULL : p;
}

static void* carve(GCPosixHeap* heap, uint32_t sizeClass)
{
	const size_t block = kHeader + class_size(sizeClass);
	if ((size_t)(heap->end - heap->next) < block)
	{
		// The rest of the old chunk is dropped: at most one block's worth, once per chunk.
		uint8_t* chunk = (uint8_t*)map_zeroed(kChunk);
		if (!chunk) return NULL;
		heap->next = chunk;
		heap->end = chunk + kChunk;
	}
	uint8_t* at = heap->next;
	heap->next += block;
	return at + kHeader;
}

void* gc_posix_heap_alloc(GCPosixHeap* heap, size_t size)
{
	if (size == 0) size = 1;
	const uint32_t sizeClass = class_of(size);
	void* p = NULL;
	if (sizeClass == kLarge)
	{
		uint8_t* mapping = (uint8_t*)map_zeroed(page_round(kHeader + size));
		if (!mapping) return NULL;
		p = mapping + kHeader;
	}
	else if (heap->freeLists[sizeClass])
	{
		p = heap->freeLists[sizeClass];
		heap->freeLists[sizeClass] = *(void**)p;
		memset(p, 0, class_size(sizeClass));
	}
	else if (!(p = carve(heap, sizeClass))) return NULL;
	BlockHeader* header = header_of(p);
	header->requested = size;
	header->sizeClass = sizeClass;
	return p;
}

void gc_posix_heap_free(GCPosixHeap* heap, void* p)
{
	if (!p) return;
	BlockHeader* header = header_of(p);
	if (header->sizeClass == kLarge)
	{
		munmap(header, page_round(kHeader + header->requested));
		return;
	}
	*(void**)p = heap->freeLists[header->sizeClass];
	heap->freeLists[header->sizeClass] = p;
}

void* gc_posix_heap_realloc(GCPosixHeap* heap, void* p, size_t size)
{
	if (!p) return gc_posix_heap_alloc(heap, size);
	if (size == 0) size = 1;
	BlockHeader* header = header_of(p);
	const size_t held = header->requested;
	const size_t room = header->sizeClass == kLarge ? page_round(kHeader + held) - kHeader : class_size(header->sizeClass);
	if (size <= room && header->sizeClass != kLarge)
	{
		if (size > held) memset((uint8_t*)p + held, 0, size - held);
		header->requested = size;
		return p;
	}
	void* moved = gc_posix_heap_alloc(heap, size);
	if (!moved) return NULL;
	memcpy(moved, p, held < size ? held : size);
	gc_posix_heap_free(heap, p);
	return moved;
}
