// The open-addressing table HashMap and HashSet are made of, in the style of Swiss tables: entries in slots and one
// control byte per slot - empty, removed, or 7 bits of the hash of the entry the slot holds. A lookup compares 16
// control bytes at a time and opens only the entries whose 7 bits match; it goes group by group, in growing steps
// over a power-of-two table, until a group has an empty slot, which a table at most 7/8 full always has. The
// container keeps its entries, typed, beside its HashTable; code that knows the entry type only at run time
// (Serialization's reflection codec) works on the same parts through these functions.

include "hash.ch"

namespace Core
{
	const int64 kControlGroup = 16;    // control bytes compared at once
	const int8 kEmptyControl = -128;
	const int8 kRemovedControl = -2;   // a live entry's control byte is 0 to 127
	const int64 kSmallestTable = 8;

	// A table's control bytes and counts; its entries are the container's.
	struct HashTable
	{
		public int8* control;   // capacity + kControlGroup bytes: one per slot, then copies of the first ones, so a
		                        // group read from any slot stays inside
		public int64 count;     // live entries
		public int64 used;      // live and removed slots
		public int64 capacity;  // zero or a power of two, at least kSmallestTable
	}

	// Where a hash's probe starts, and the 7 bits its entry's control byte holds.
	uint64 ProbeStart(uint64 hash) { return hash >> 7; }
	int8 HashControl(uint64 hash) { return (int8)(hash & 127u); }

	// The control bytes of the group at `slot` that are `value`: bit i for slot + i.
	uint32 GroupMatching(const int8* control, int64 slot, int8 value) { return simd_bitmask(simd_load(control + slot, 16) == value); }
	// Its empty slots, and its free ones (empty or removed).
	uint32 GroupEmpty(const int8* control, int64 slot) { return GroupMatching(control, slot, kEmptyControl); }
	uint32 GroupFree(const int8* control, int64 slot) { return simd_bitmask(simd_load(control + slot, 16) < (int8)0); }

	// A probe's place: the group it reads, and how far the next one is.
	struct Probe
	{
		public int64 slot;
		public int64 step;
		public int64 mask;
		public void Next() { step += kControlGroup; slot = (slot + step) & mask; }
		public int64 SlotAt(uint32 bits) const { return (slot + (int64)bit_trailing_zeros(bits)) & mask; }
	}

	Probe ProbeFor(const HashTable& table, uint64 hash);

	// The slot of the first entry `matches(slot)` accepts on `hash`'s probe, or -1 when the probe reaches an empty slot
	// first.
	template<F> int64 FindSlot(const HashTable& table, uint64 hash, F matches);
	// The first empty or removed slot on `hash`'s probe; a table is never full, so there is one.
	int64 FreeSlot(const HashTable& table, uint64 hash);

	// A free slot taken by an entry of `hash`, and a live one given back.
	void Occupy(HashTable& table, int64 slot, uint64 hash);
	void Vacate(HashTable& table, int64 slot);
	bool IsLive(const HashTable& table, int64 slot) { return table.control[slot] >= (int8)0; }
	// The first live slot from `slot` on, or the capacity.
	int64 NextLiveSlot(const HashTable& table, int64 slot);

	// Whether one more entry would fill the table past 7/8 (counting removed slots).
	bool NeedsRoom(const HashTable& table) { return (table.used + 1) * 8 > table.capacity * 7; }
	// The capacity that holds `entries` at most 7/8 full.
	int64 CapacityFor(int64 entries);
	// The capacity a full table moves to: room for twice its live entries; the removed slots are left behind.
	int64 GrownCapacity(const HashTable& table);

	// A fresh table of `capacity` empty slots, and its entries: zeroed, `entryBytes` each, aligned to `entryAlign` and
	// registered with the GC (an entry may hold pointers). The previous control bytes and entries must have been taken
	// or freed.
	void* AllocateTable(HashTable& table, int64 capacity, int64 entryBytes, int64 entryAlign);
	// Gives the control bytes and `entries` back (the live entries must be destroyed first) and leaves the table zero.
	void FreeTable(HashTable& table, void* entries, int64 entryAlign);
	// Every slot empty again, the storage kept (the live entries must be destroyed first).
	void EmptyTable(HashTable& table);
}
