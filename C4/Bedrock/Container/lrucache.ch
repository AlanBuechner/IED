// LruCache<K, V>: a map that holds at most `capacity` entries and, when full, makes room by dropping the one used least
// recently (decoded assets, glyphs, query results). A HashMap finds an entry's node in a List kept from most to least
// recently used, so every operation is O(1) and an entry never moves in memory. Keys hash and compare through
// KeyTraits<K>. Range-for visits the entries from the most to the least recently used, without touching them.
//     Core::LruCache<Core::String, Texture*> textures(256);
//     Texture** cached = textures.Find(name);              // marks it used
//     if (!cached) textures.Put(name, Load(name));         // may drop the least recently used

include "hashmap.ch"
include "list.ch"

namespace Core
{
	template<K, V> class LruCache
	{
	public:
		[operator("create")] public void Init(int64 capacity) { limit = capacity < 1 ? 1 : capacity; slots.Reserve(limit); }
		[operator("copy")] public void CopyFrom(const LruCache<K, V>& other);

		public int64 Count() const { return order.Count(); }
		public int64 Capacity() const { return limit < 1 ? 1 : limit; }   // a zero cache holds one entry
		public bool IsEmpty() const { return order.IsEmpty(); }

		// The value of `key`, marked as the most recently used; null when absent. Valid until the cache changes.
		public V* Find(const K& key);
		// The value of `key` without marking it used; null when absent.
		public const V* Peek(const K& key) const;
		public bool Contains(const K& key) const { return slots.Contains(key); }
		// Adds or replaces `key`'s value as the most recently used. True when an entry was dropped to make room.
		public bool Put(K key, V value);
		public bool Remove(const K& key);
		public void Clear();
		// Holds at most `capacity` entries from now on, dropping the least recently used ones that no longer fit.
		public void Resize(int64 capacity);

		[operator("begin")] public ConstListIterator<KeyValue<K, V>> Begin() const { return order.Begin(); }
		[operator("end")] public ConstListIterator<KeyValue<K, V>> End() const { return order.End(); }

	private:
		private void DropLeastRecent();

		private List<KeyValue<K, V>> order;                      // most recently used first
		private HashMap<K, ListNode<KeyValue<K, V>>*> slots;     // each key's node in `order`
		private int64 limit;
	}
}
