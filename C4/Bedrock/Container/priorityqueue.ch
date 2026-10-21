// PriorityQueue<T, Before>: a binary heap over an Array (std::priority_queue). Push puts an element in and Pop takes out
// the one no other comes before: with the default Ascending<T> the smallest by <, so Pops come in ascending order (a
// timer queue, Dijkstra's frontier). `Before` is any type with [operator("call")] bool(const T&, const T&); give an
// instance with state to Init. A zero queue is empty and allocates nothing. Range-for visits the elements in heap
// order, not sorted. Top is unchecked.
//     Core::PriorityQueue<int64> timers;
//     timers.Push(30); timers.Push(10); timers.Push(20);
//     int64 next = timers.Pop();   // 10

include "array.ch"
include "sort.ch"

namespace Core
{
	template<T, Before = Ascending<T>> class PriorityQueue
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { items.Reserve(capacity); }
		[operator("create")] public void Init(Before order) { before = order; }

		public int64 Count() const { return items.Count(); }
		public int64 Capacity() const { return items.Capacity(); }
		public bool IsEmpty() const { return items.IsEmpty(); }

		// Copies `value` in, or moves it: Push(take(x)) puts in an element that cannot be copied.
		public void Push(T value);
		// The first element moved out; a zero T when empty.
		public T Pop();
		// The first element, left in place.
		public const T& Top() const { return items[0]; }
		public void Clear() { items.Clear(); }
		public void Reserve(int64 elements) { items.Reserve(elements); }

		[operator("begin")] public ConstArrayIterator<T> Begin() const { return items.Begin(); }
		[operator("end")] public ConstArrayIterator<T> End() const { return items.End(); }

	private:
		// Moves the element at `index` toward the root (after a push) or the leaves (after a pop) to where it belongs.
		private void SiftUp(int64 index);
		private void SiftDown(int64 index);
		private void SwapAt(int64 a, int64 b);

		private Array<T> items;
		private Before before;
	}
}
