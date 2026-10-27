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

	// `count` is what AllocateElements was asked for.
	template<T> void FreeElements(T* elements, int64 count) {
		if ((uint64)alignof(T) > kRawAllocAlignment) raw_free_aligned((void*)elements);
		else raw_free_sized((void*)elements, (uint64)(count * (int64)sizeof(T)));
	}

	// A buffer of `newBytes` in place of `elements` (`oldBytes`, null for none), its first `keepBytes` kept. The collector
	// scans it only when `scanned`: when its elements may hold managed pointers (holds_pointers), so a buffer of numbers
	// costs a collection nothing. Past 1 MiB it grows in place (buffer_resize); over-aligned elements move into a block of
	// their own, registered before they arrive. A buffer keeps the form it was made with.
	void* ResizeElementBytes(void* elements, uint64 oldBytes, uint64 keepBytes, uint64 newBytes, uint64 align, bool scanned) {
		if (align <= kRawAllocAlignment)
			return scanned ? buffer_resize_scanned(elements, oldBytes, keepBytes, newBytes) : buffer_resize(elements, oldBytes, keepBytes, newBytes);
		void* moved = newBytes > 0u ? raw_alloc_aligned(newBytes, align) : nullptr;
		if (moved && scanned) gc_register_region(moved, newBytes);
		if (keepBytes > 0u) memcpy(moved, elements, keepBytes);
		if (elements) {
			if (scanned) gc_unregister_region(elements);
			raw_free_aligned(elements);
		}
		return moved;
	}

	// AllocateElements' storage, registered with the collector when its elements may hold pointers; FreeScannable
	// releases it.
	template<T> T* AllocateScannable(int64 count) {
		T* elements = AllocateElements<T>(count);
		if (elements && holds_pointers(T)) gc_register_region((void*)elements, (uint64)(count * (int64)sizeof(T)));
		return elements;
	}

	template<T> void FreeScannable(T* elements, int64 count) {
		if (!elements) return;
		if (holds_pointers(T)) gc_unregister_region((void*)elements);
		FreeElements<T>(elements, count);
	}

	// Element ranges, given as a pointer and bounds rather than read from a container's fields: a byte stored into an
	// element may alias anything, so a loop over a container's own `data` reloads it after every store and never
	// becomes a memset or memcpy.
	template<T> void CreateElements(T* items, int64 first, int64 end) {
		for (int64 k = first; k < end; ++k) new (&items[k]) T();
	}

	template<T> void CopyElements(T* to, const T* from, int64 count) {
		for (int64 k = 0; k < count; ++k) new (&to[k]) T(from[k]);
	}

	template<T> void DestroyElements(T* items, int64 first, int64 end) {
		for (int64 k = first; k < end; ++k) destroy(&items[k]);
	}

	template<T> T* ResizeElements(T* elements, int64 oldCount, int64 keepCount, int64 newCount) {
		const uint64 size = (uint64)sizeof(T);
		return (T*)ResizeElementBytes((void*)elements, (uint64)oldCount * size, (uint64)keepCount * size, (uint64)newCount * size, (uint64)alignof(T),
			holds_pointers(T));
	}
}
