// Unmanaged memory and the byte helpers behind the raw_alloc / raw_free / byte_* builtins.

#include "GCRuntimeC.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

// Each block counts as native memory at its size, the same at its allocation and its free. MSVC's _msize gives back
// the size asked for; glibc knows only the size it gave (malloc_usable_size, at least that), so a Linux block counts
// at that size both times, whatever size the caller passes to the sized free.
#if defined(_WIN32)
static size_t taken_size(void* block, size_t asked) { (void)block; return asked; }
static size_t given_size(void* block, size_t asked) { return asked ? asked : _msize(block); }
#else
static size_t taken_size(void* block, size_t asked) { (void)asked; return malloc_usable_size(block); }
static size_t given_size(void* block, size_t asked) { (void)asked; return malloc_usable_size(block); }
#endif

void* c4_raw_alloc(size_t size)
{
	const size_t bytes = size ? size : 1;
	void* block = malloc(bytes);
	if (block) c4_gc_native_taken(taken_size(block, bytes));
	return block;
}

void c4_raw_free(void* ptr)
{
	if (!ptr) return;
	c4_gc_native_given(given_size(ptr, 0));
	free(ptr);
}

// The same for a block whose size the caller knows (what it asked c4_raw_alloc for): no heap lookup on Windows.
void c4_raw_free_sized(void* ptr, size_t size)
{
	if (!ptr) return;
	c4_gc_native_given(given_size(ptr, size ? size : 1));
	free(ptr);
}

// An aligned block keeps, just before it, the heap block it lies in and that block's size.
typedef struct AlignedBlock
{
	void* block;
	size_t bytes;
} AlignedBlock;

void* c4_raw_alloc_aligned(size_t size, size_t align)
{
	const size_t alignment = align < sizeof(AlignedBlock) ? sizeof(AlignedBlock) : align;
	const size_t bytes = (size ? size : 1) + alignment + sizeof(AlignedBlock);
	uint8_t* block = (uint8_t*)malloc(bytes);
	if (!block) return NULL;
	const uintptr_t first = ((uintptr_t)block + sizeof(AlignedBlock) + alignment - 1) & ~(uintptr_t)(alignment - 1);
	AlignedBlock* header = (AlignedBlock*)first - 1;
	header->block = block;
	header->bytes = bytes;
	c4_gc_native_taken(bytes);
	return (void*)first;
}

void c4_raw_free_aligned(void* ptr)
{
	if (!ptr) return;
	const AlignedBlock header = ((const AlignedBlock*)ptr)[-1];
	c4_gc_native_given(header.bytes);
	free(header.block);
}

void c4_byte_copy(void* dst, const void* src, int64_t len, int64_t dstOff, int64_t srcOff)
{
	if (!dst || !src || len <= 0) return;
	memcpy((char*)dst + dstOff, (const char*)src + srcOff, (size_t)len);
}

void c4_byte_move(void* dst, const void* src, int64_t len, int64_t dstOff, int64_t srcOff)
{
	if (!dst || !src || len <= 0) return;
	memmove((char*)dst + dstOff, (const char*)src + srcOff, (size_t)len);
}

int32_t c4_byte_at(const void* p, int64_t index)
{
	return (int32_t)((const unsigned char*)p)[index];
}

void c4_set_byte(void* p, int64_t index, int32_t value)
{
	((unsigned char*)p)[index] = (unsigned char)value;
}
