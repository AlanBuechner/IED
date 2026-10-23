// HashSet<T>: an unordered set (std::unordered_set) on the same open-addressing table as HashMap (hashtable.ch). Values
// hash and compare through Traits, by default KeyTraits<T>. The set constructs, copies, relocates and destroys its
// values itself and registers them with the GC. A zero HashSet is empty and allocates nothing. Range-for visits the
// values in table order, read only: a value is found by its hash, so it is not changed in place.
//     Core::HashSet<Core::String> seen;
//     if (!seen.Add(Core::String(name))) printf("duplicate %s\n", name);
//     bool known = seen.ContainsAs(Core::StringView("ada"));   // looked up by a view: no String is made

include "hashtable.ch"
include "initlist.ch"

namespace Core
{
	template<T> struct HashSetIterator {
		public const T* values;
		public const int8* control;
		public int64 index;
		public int64 end;
		[operator("notequal")] public bool NotEqual(const HashSetIterator<T>& other) const { return index != other.index; }
		[operator("dereference")] public const T& Element() const { return values[index]; }
		[operator("increment")] public HashSetIterator<T> Next();
	}

	template<T, Traits = KeyTraits<T>> class HashSet
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { Reserve(capacity); }
		[operator("create")] public void Init(InitList<T> values);
		[operator("copy")] public void CopyFrom(const HashSet<T, Traits>& other);
		[operator("delete")] public void Delete();
		// Its table, and what its values hold.
		[operator("native_bytes")] public int64 NativeBytes() const { return TableBytes(table, (int64)sizeof(T)) + LiveNativeBytes(table, values); }

		public int64 Count() const { return table.count; }
		public int64 Capacity() const { return table.capacity; }
		public bool IsEmpty() const { return table.count == 0; }

		// True when the value was added, false when an equal one was there already (the set keeps that one).
		// Copies `value` in, or moves it: Add(take(x)) puts in a value that cannot be copied.
		public bool Add(T value);
		public bool Contains(const T& value) const { return SlotOf(value) >= 0; }
		// The value equal to `value`, or null; valid until the set changes.
		public const T* Find(const T& value) const;
		public bool Remove(const T& value) { return RemoveSlot(SlotOf(value)); }
		public void Clear();
		// Room for `values` values without growing again.
		public void Reserve(int64 values);

		// Lookups by a value of another type Q that hashes as T does (LookupTraits<T, Q>) and compares with == to a T: a
		// Core::StringView or a C string for a set of Core::String, so no String is made to look one up.
		template<Q> public bool ContainsAs(const Q& value) const { return SlotAs(value) >= 0; }
		template<Q> public bool RemoveAs(const Q& value) { return RemoveSlot(SlotAs(value)); }

		// Set algebra, in place: every value of `other` added; only the values `other` has kept; the values it has removed.
		public void UnionWith(const HashSet<T, Traits>& other);
		public void IntersectWith(const HashSet<T, Traits>& other);
		public void ExceptWith(const HashSet<T, Traits>& other);
		// Whether `other` has every value of this set.
		public bool IsSubsetOf(const HashSet<T, Traits>& other) const;

		[operator("begin")] public HashSetIterator<T> Begin() const;
		[operator("end")] public HashSetIterator<T> End() const;

	private:
		// The live slot holding a value equal to `value`, or -1.
		private int64 SlotOf(const T& value) const {
			const T* stored = values;
			return FindSlot(table, Traits::Hash(value), [stored, &value](int64 slot) -> bool { return Traits::Equal(stored[slot], value); });
		}
		template<Q> private int64 SlotAs(const Q& value) const {
			const T* stored = values;
			return FindSlot(table, LookupTraits<T, Q>::Hash(value), [stored, &value](int64 slot) -> bool { return stored[slot] == value; });
		}
		private bool RemoveSlot(int64 slot);
		private void Rehash(int64 capacity);

		private T* values;   // capacity values; the table says which are live
		private HashTable table;
	}
}
