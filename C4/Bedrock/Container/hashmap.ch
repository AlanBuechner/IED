// HashMap<K, V>: an unordered map (std::unordered_map) over the open-addressing table of hashtable.ch, Swiss-table
// style: a lookup compares 16 control bytes at once and opens only the entries whose hash bits match. Keys hash and
// compare through Traits, by default KeyTraits<K>: K's own [operator("hash")] and ==, with built-in traits for
// numbers, characters, bool and pointers; pass ByteKeyTraits<K> for a plain-data struct key. The map constructs,
// copies, relocates and destroys its keys and values itself and registers its entries with the GC. A zero HashMap is
// empty and allocates nothing.
//     Core::HashMap<Core::String, int32> ages;
//     ages.Insert(Core::String("ada"), 36);
//     ages[Core::String("alan")] = 41;          // added when absent
//     int32* age = ages.FindAs(Core::StringView("ada"));   // looked up by a view: no String is made
//     for (Core::KeyValue<Core::String, int32>& entry : ages) printf("%s %d\n", entry.key.CStr(), entry.value);

include "hashtable.ch"

namespace Core
{
	template<K, V> struct KeyValue {
		public K key;     // do not change a key through an iterator: the table finds it by its hash
		public V value;
	}

	// Visits the live entries in table order.
	template<K, V> struct HashMapIterator {
		public KeyValue<K, V>* entries;
		public const int8* control;
		public int64 index;
		public int64 end;
		[operator("notequal")] public bool NotEqual(const HashMapIterator<K, V>& other) const { return index != other.index; }
		[operator("dereference")] public KeyValue<K, V>& Element() { return entries[index]; }
		[operator("increment")] public HashMapIterator<K, V> Next();
	}

	// The same over a const map: the entries read only.
	template<K, V> struct ConstHashMapIterator {
		public const KeyValue<K, V>* entries;
		public const int8* control;
		public int64 index;
		public int64 end;
		[operator("notequal")] public bool NotEqual(const ConstHashMapIterator<K, V>& other) const { return index != other.index; }
		[operator("dereference")] public const KeyValue<K, V>& Element() const { return entries[index]; }
		[operator("increment")] public ConstHashMapIterator<K, V> Next();
	}

	template<K, V, Traits = KeyTraits<K>> class HashMap
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { Reserve(capacity); }
		[operator("copy")] public void CopyFrom(const HashMap<K, V, Traits>& other);
		[operator("delete")] public void Delete();
		// Its table, and what its live entries hold.
		[operator("native_bytes")] public int64 NativeBytes() const { return TableBytes(table, (int64)sizeof(KeyValue<K, V>)) + LiveNativeBytes(table, entries); }

		public int64 Count() const { return table.count; }
		public int64 Capacity() const { return table.capacity; }
		public bool IsEmpty() const { return table.count == 0; }

		// True when the key was added, false when its value was replaced.
		// Copies `value` in, or moves it: Insert(key, take(x)) puts in a value that cannot be copied.
		public bool Insert(K key, V value);
		// The value of `key`, added (default-created) when absent.
		[operator("index")] public V& At(const K& key);
		// The value of `key`, or null; valid until the map changes.
		public V* Find(const K& key);
		public const V* Find(const K& key) const;
		// Copies the value of `key` into `out`; false, with `out` untouched, when absent.
		public bool TryGet(const K& key, V& out) const;
		// A copy of the value of `key`; a zero V when absent.
		public V Get(const K& key) const;
		public bool Contains(const K& key) const { return SlotOf(key) >= 0; }
		public bool Remove(const K& key) { return RemoveSlot(SlotOf(key)); }
		public void Clear();
		// Room for `entries` entries without growing again.
		public void Reserve(int64 entries);

		// Lookups by a key of another type Q that hashes as K does (LookupTraits<K, Q>) and compares with == to a K: a
		// Core::StringView or a C string for Core::String keys, so no String is made to look one up.
		template<Q> public V* FindAs(const Q& key) { int64 slot = SlotAs(key); return slot >= 0 ? &entries[slot].value : nullptr; }
		template<Q> public const V* FindAs(const Q& key) const { int64 slot = SlotAs(key); return slot >= 0 ? &entries[slot].value : nullptr; }
		template<Q> public bool ContainsAs(const Q& key) const { return SlotAs(key) >= 0; }
		template<Q> public bool RemoveAs(const Q& key) { return RemoveSlot(SlotAs(key)); }

		[operator("begin")] public HashMapIterator<K, V> Begin();
		[operator("end")] public HashMapIterator<K, V> End();
		[operator("begin")] public ConstHashMapIterator<K, V> Begin() const;
		[operator("end")] public ConstHashMapIterator<K, V> End() const;

	private:
		// The live slot holding `key`, or -1.
		private int64 SlotOf(const K& key) const {
			const KeyValue<K, V>* stored = entries;
			return FindSlot(table, Traits::Hash(key), [stored, &key](int64 slot) -> bool { return Traits::Equal(stored[slot].key, key); });
		}
		template<Q> private int64 SlotAs(const Q& key) const {
			const KeyValue<K, V>* stored = entries;
			return FindSlot(table, LookupTraits<K, Q>::Hash(key), [stored, &key](int64 slot) -> bool { return stored[slot].key == key; });
		}
		// Where a new key of `hash` goes, the table first grown when it is full.
		private int64 SlotForNew(uint64 hash);
		private bool RemoveSlot(int64 slot);
		private void Rehash(int64 capacity);

		// Code that knows V only at run time (Serialization's reflection codec) finds these two by reflection and reads
		// and builds the map through the functions of hashtable.ch.
		private KeyValue<K, V>* entries;   // capacity entries; the table says which are live
		private HashTable table;
	}
}
