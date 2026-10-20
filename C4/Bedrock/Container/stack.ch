// Stack<T>: last in, first out (std::stack), over an Array. Range-for visits the elements from the bottom
// to the top. Top is unchecked.

include "array.ch"

namespace Core
{
	template<T> class Stack
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { items.Reserve(capacity); }

		public int64 Count() const { return items.Count(); }
		public int64 Capacity() const { return items.Capacity(); }
		public bool IsEmpty() const { return items.IsEmpty(); }

		public void Push(const T& value) { items.Push(value); }
		public T Pop() { return items.Pop(); }   // the top moved out; a zero T when empty
		public T& Top() { return items.Back(); }
		public const T& Top() const { return items.Back(); }
		public void Clear() { items.Clear(); }

		[operator("begin")] public ArrayIterator<T> Begin() { return items.Begin(); }
		[operator("end")] public ArrayIterator<T> End() { return items.End(); }
		[operator("begin")] public ConstArrayIterator<T> Begin() const { return items.Begin(); }
		[operator("end")] public ConstArrayIterator<T> End() const { return items.End(); }

	private:
		private Array<T> items;
	}
}
