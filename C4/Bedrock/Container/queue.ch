// Queue<T>: first in, first out (std::queue), over a ring buffer whose capacity is a power of two, so
// wrapping is a mask. It constructs, relocates and destroys its elements itself and registers its buffer
// with the GC. A zero Queue is empty and allocates nothing. Range-for visits the elements from the front
// to the back. Front and Back are unchecked.

include "storage.ch"

namespace Core
{
	template<T> struct QueueIterator {
		public T* items;
		public int64 mask;
		public int64 position;   // head + elements visited, unwrapped
		[operator("notequal")] public bool NotEqual(const QueueIterator<T>& other) const { return position != other.position; }
		[operator("dereference")] public T& Element() { return items[position & mask]; }
		[operator("increment")] public QueueIterator<T> Next() { QueueIterator<T> next = *this; next.position += 1; return next; }
	}

	template<T> class Queue
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { Reserve(capacity); }
		[operator("copy")] public void CopyFrom(const Queue<T>& other);
		[operator("delete")] public void Delete();

		public int64 Count() const { return count; }
		public int64 Capacity() const { return capacity; }
		public bool IsEmpty() const { return count == 0; }

		public void Push(T value);   // copies `value` in, or moves it: Push(take(x))
		public T Pop();   // the front moved out; a zero T when empty
		public T& Front() { return data[head]; }
		public const T& Front() const { return data[head]; }
		public T& Back() { return data[(head + count - 1) & (capacity - 1)]; }
		public const T& Back() const { return data[(head + count - 1) & (capacity - 1)]; }
		public void Clear();
		public void Reserve(int64 capacity);   // rounded up to a power of two

		[operator("begin")] public QueueIterator<T> Begin() { QueueIterator<T> it; it.items = data; it.mask = capacity - 1; it.position = head; return it; }
		[operator("end")] public QueueIterator<T> End() { QueueIterator<T> it; it.items = data; it.mask = capacity - 1; it.position = head + count; return it; }

	private:
		private T* data;
		private int64 count;
		private int64 capacity;   // zero or a power of two
		private int64 head;       // where the front lives
	}
}
