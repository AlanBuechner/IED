// Virtual memory (D-63): reserve address space, commit and decommit pages in it, change their rights, give it back.
// The functions take raw addresses, round sizes out to whole pages and say false (or null) when the system
// refuses. The collector never scans this memory: GC::RegisterRegion a range that holds managed pointers.
//     uint8* arena = (uint8*)Platform::Reserve(1 << 30);   // 1 GiB of addresses, no memory yet
//     Platform::Commit((void*)arena, 1 << 20, Platform::Protection::ReadWrite);
//     Platform::Release((void*)arena);

namespace Platform
{
	// The rights of a page; x64 has no write-only page, so writing implies reading. The runtime's kProtect*.
	enum class Protection { None, Read, ReadWrite, Execute, ReadExecute, ReadWriteExecute }

	enum class RegionState { Free, Reserved, Committed }   // the runtime's kRegion*

	// A run of pages that share their state and rights.
	struct MemoryRegion
	{
		public void* base;
		public int64 size;
		public void* allocationBase;    // what Reserve or Allocate returned for its reservation; null when free
		public RegionState state;
		public Protection protection;   // None unless committed; copy-on-write pages read as writable
	}

	[link("c4_vm_page_size")] int64 PageSize() noexcept;
	// Reservations start at a multiple of this (64 KiB on Windows).
	[link("c4_vm_allocation_granularity")] int64 AllocationGranularity() noexcept;

	// `size` bytes of addresses and no memory; null when size <= 0 or there is no room.
	void* Reserve(int64 size);
	// The same, starting at a multiple of `alignment`, a power of two (null for any other alignment).
	void* ReserveAligned(int64 size, int64 alignment);
	// A reservation committed whole.
	void* Allocate(int64 size, Protection protection);
	// Backs reserved pages with memory, zeroed when new (committed pages keep their bytes), and gives them `protection`.
	bool Commit(void* address, int64 size, Protection protection);
	// Gives the pages' memory back; the addresses stay reserved and read as zero once committed again.
	bool Decommit(void* address, int64 size);
	// Gives back a whole reservation: `base` is what Reserve, ReserveAligned or Allocate returned.
	bool Release(void* base);
	// Changes the rights of committed pages.
	bool Protect(void* address, int64 size, Protection protection);
	// The run of pages holding `address`; false when the address is outside the process's space.
	bool Query(const void* address, MemoryRegion& region);
	// Makes code written to memory visible to the processor's instruction fetch: call it before running the code.
	[link("c4_vm_flush_instruction_cache")] void FlushInstructionCache(const void* address, int64 size) noexcept;
}
