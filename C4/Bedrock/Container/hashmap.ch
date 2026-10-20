// HashMap<K, V>: an unordered map (std::unordered_map) with open addressing and linear probing over a
// power-of-two table. Keys hash and compare through Traits, by default KeyTraits<K>: K's own
// [operator("hash")] and ==, with built-in traits for numbers, characters, bool and pointers; pass
// ByteKeyTraits<K> for a plain-data struct key. The map constructs, copies, relocates and destroys its
// keys and values itself and registers its entries with the GC. A zero HashMap is empty and allocates
// nothing.
//     Core::HashMap<Core::String, int32> ages;
//     ages.Insert(Core::String("ada"), 36);
//     ages[Core::String("alan")] = 41;          // added when absent
//     int32* age = ages.Find(Core::String("ada"));
//     for (Core::KeyValue<Core::String, int32>& entry : ages) printf("%s %d\n", entry.key.CStr(), entry.value);

include "hash.ch"
include "storage.ch"

namespace Core
{
	template<K, V> struct KeyValue {
		public K key;     // do not change a key through an iterator: the table finds it by its hash
		public V value;
	}

	// Visits the live entries in table order.
	template<K, V> struct HashMapIterator {
		public KeyValue<K, V>* entries;
		public const uint8* states;
		public int64 index;
		public int64 end;
		[operator("notequal")] public bool NotEqual(const HashMapIterator<K, V>& other) const { return index != other.index; }
		[operator("dereference")] public KeyValue<K, V>& Element() { return entries[index]; }
		[operator("increment")] public HashMapIterator<K, V> Next();
	}

	template<K, V, Traits = KeyTraits<K>> class HashMap
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { Reserve(capacity); }
		[operator("copy")] public void CopyFrom(const HashMap<K, V, Traits>& other);
		[operator("delete")] public void Delete();

		public int64 Count() const { return count; }
		public int64 Capacity() const { return capacity; }
		public bool IsEmpty() const { return count == 0; }

		// True when the key was added, false when its value was replaced.
		public bool Insert(const K& key, const V& value);
		// The value of `key`, added (default-created) when absent.
		[operator("index")] public V& At(const K& key);
		// The value of `key`, or null; valid until the map changes.
		public V* Find(const K& key);
		public const V* Find(const K& key) const;
		// Copies the value of `key` into `out`; false, with `out` untouched, when absent.
		public bool TryGet(const K& key, V& out) const;
		// A copy of the value of `key`; a zero V when absent.
		public V Get(const K& key) const;
		public bool Contains(const K& key) const { return Slot(key) >= 0; }
		public bool Remove(const K& key);
		public void Clear();
		// Room for `entries` entries without growing again.
		public void Reserve(int64 entries);

		[operator("begin")] public HashMapIterator<K, V> Begin();
		[operator("end")] public HashMapIterator<K, V> End();

	private:
		private int64 Slot(const K& key) const;         // the live slot holding key, or -1
		private int64 FreeSlot(const K& key) const;     // where key goes: its probe's first tombstone or empty slot
		private void Rehash(int64 capacity);
		// Live and removed slots stay at most 3/4 of the table, so every probe meets an empty slot.
		private void MakeRoom();

		private KeyValue<K, V>* entries;
		private uint8* states;   // per slot: kEmptySlot, kLiveSlot or kRemovedSlot
		private int64 count;     // live entries
		private int64 used;      // live and removed slots
		private int64 capacity;  // zero or a power of two
	}

	const uint8 kEmptySlot = 0;
	const uint8 kLiveSlot = 1;
	const uint8 kRemovedSlot = 2;
}
