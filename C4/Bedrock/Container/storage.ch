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

	// A buffer the collector scans, of `newBytes` in place of `elements` (`oldBytes`, null for none), its first
	// `keepBytes` kept. Past 1 MiB it grows in place (buffer_resize_scanned); over-aligned elements move into a block
	// of their own, registered before they arrive.
	void* ResizeScannedBytes(void* elements, uint64 oldBytes, uint64 keepBytes, uint64 newBytes, uint64 align) {
		if (align <= kRawAllocAlignment) return buffer_resize_scanned(elements, oldBytes, keepBytes, newBytes);
		void* moved = newBytes > 0u ? raw_alloc_aligned(newBytes, align) : nullptr;
		if (moved) gc_register_region(moved, newBytes);
		if (keepBytes > 0u) memcpy(moved, elements, keepBytes);
		if (elements) { gc_unregister_region(elements); raw_free_aligned(elements); }
		return moved;
	}

	template<T> T* ResizeScannedElements(T* elements, int64 oldCount, int64 keepCount, int64 newCount) {
		const uint64 size = (uint64)sizeof(T);
		return (T*)ResizeScannedBytes((void*)elements, (uint64)oldCount * size, (uint64)keepCount * size, (uint64)newCount * size, (uint64)alignof(T));
	}
}
