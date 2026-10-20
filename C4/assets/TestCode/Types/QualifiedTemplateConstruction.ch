// Types - constructing a namespaced template by its qualified name (BUG-169)
namespace QtcSpace {
	template<T> struct Box {
		public T value;
		[operator("create")] public void Init(T v) { value = v; }
	}
	template<K, V> struct Pair {
		public K key;
		public V value;
		[operator("create")] public void Init(K k, V v) { key = k; value = v; }
	}
	namespace Inner {
		template<T> struct Cell {
			public T value;
			[operator("create")] public void Init(T v) { value = v; }
		}
	}
}

void TestQualifiedTemplateConstruction();
