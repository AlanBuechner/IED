// Raw storage for a container's elements, aligned to the element type: a 32- or 64-byte vector (`packed float[8]`)
// is read with aligned instructions. Memory from AllocateElements is released with FreeElements of the same type.

namespace Core
{
	// What raw_alloc (malloc) guarantees on Win64.
	const uint64 kRawAllocAlignment = 16;

	template<T> T* AllocateElements(int64 count) {
		const uint64 bytes = (uint64)(count * (int64)sizeof(T));
		if ((uint64)alignof(T) > kRawAllocAlignment) return (T*)raw_alloc_aligned(bytes, (uint64)alignof(T));
		return (T*)raw_alloc(bytes);
	}

	template<T> void FreeElements(T* elements) {
		if ((uint64)alignof(T) > kRawAllocAlignment) raw_free_aligned((void*)elements);
		else raw_free((void*)elements);
	}
}
