// Unmanaged memory and the byte helpers behind the raw_alloc / raw_free / byte_* builtins.

#include "GCRuntimeC.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

void* c4_raw_alloc(size_t size)
{
	return malloc(size ? size : 1);
}

void c4_raw_free(void* ptr)
{
	free(ptr);
}

void* c4_raw_alloc_aligned(size_t size, size_t align)
{
	return _aligned_malloc(size ? size : 1, align < sizeof(void*) ? sizeof(void*) : align);
}

void c4_raw_free_aligned(void* ptr)
{
	_aligned_free(ptr);
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
