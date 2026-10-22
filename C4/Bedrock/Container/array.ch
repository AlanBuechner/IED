// Array<T>: a growable contiguous array (std::vector). It constructs, copies, relocates and destroys its
// elements itself, so element types with a create, copy or delete behave as in C++, and it registers its
// buffer with the GC so references held in it stay alive. A zero Array is empty and allocates nothing.
// Element access is unchecked.
//     Core::Array<int32> numbers = { 3, 1, 2 };
//     numbers.Push(4);
//     numbers.Sort();
//     for (int32 n : numbers) printf("%d\n", n);

include "initlist.ch"
include "radixsort.ch"
include "storage.ch"

namespace Core
{
	template<T> struct ArrayIterator {
		public T* items;
		public int64 index;
		[operator("notequal")] public bool NotEqual(const ArrayIterator<T>& other) const { return index != other.index; }
		[operator("dereference")] public T& Element() { return items[index]; }
		[operator("increment")] public ArrayIterator<T> Next() { ArrayIterator<T> next = *this; next.index += 1; return next; }
	}

	// What a const Array hands out: its elements are read-only.
	template<T> struct ConstArrayIterator {
		public const T* items;
		public int64 index;
		[operator("notequal")] public bool NotEqual(const ConstArrayIterator<T>& other) const { return index != other.index; }
		[operator("dereference")] public const T& Element() const { return items[index]; }
		[operator("increment")] public ConstArrayIterator<T> Next() { ConstArrayIterator<T> next = *this; next.index += 1; return next; }
	}

	template<T> class Array
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 capacity) { Reserve(capacity); }
		[operator("create")] public void Init(InitList<T> items);
		[operator("copy")] public void CopyFrom(const Array<T>& other);
		[operator("delete")] public void Delete();

		public int64 Count() const { return count; }
		public int64 Capacity() const { return capacity; }
		public int64 ByteSize() const { return count * (int64)sizeof(T); }
		public bool IsEmpty() const { return count == 0; }

		[operator("index")] public T& At(int64 i) { return data[i]; }
		[operator("index")] public const T& At(int64 i) const { return data[i]; }
		public T& Front() { return data[0]; }
		public const T& Front() const { return data[0]; }
		public T& Back() { return data[count - 1]; }
		public const T& Back() const { return data[count - 1]; }
		public T* Data() { return data; }
		public const T* Data() const { return data; }

		// Copies `value` in, or moves it: Push(take(x)) puts in a value that cannot be copied. `value` may be an
		// element of this array.
		public void Push(T value);
		public T Pop();                     // the last element moved out; a zero T when empty
		public void Insert(int64 index, T value);   // as Push, at `index` (at most Count())
		public void Append(const Array<T>& other);
		public void Append(const T* items, int64 count);   // `items` may point into this array
		public void RemoveAt(int64 index);
		public void RemoveRange(int64 first, int64 end);   // elements [first, end)
		public void RemoveSwap(int64 index);               // O(1): the last element takes its place
		public void Clear();                               // destroys the elements, keeps the buffer
		public void Reserve(int64 capacity);
		public void Resize(int64 count);                   // new elements default-created, removed ones destroyed
		// Resize that leaves new elements as the memory held them: for bytes and numbers the caller writes before it
		// reads them (a decoder's output), where creating them would only zero what is about to be written.
		public void ResizeUninitialized(int64 count);
		public void ShrinkToFit();
		public void Swap(Array<T>& other);                 // exchanges the buffers, no element moves

		// The first index whose element == value, or -1.
		public int64 IndexOf(const T& value) const;
		public bool Contains(const T& value) const { return IndexOf(value) >= 0; }

		public void Sort() { AscendingSorter<T>::Sort(data, count); }   // a radix sort for numbers
		template<F> public void SortBy(F before) { Core::Sort(data, count, before); }
		public void Reverse();
		// Binary searches of a sorted array (std::lower_bound / upper_bound).
		public int64 LowerBound(const T& value) const { return Core::LowerBound(data, count, value, Ascending<T>()); }
		public int64 UpperBound(const T& value) const { return Core::UpperBound(data, count, value, Ascending<T>()); }
		template<F> public int64 LowerBoundBy(const T& value, F before) const { return Core::LowerBound(data, count, value, before); }
		template<F> public int64 UpperBoundBy(const T& value, F before) const { return Core::UpperBound(data, count, value, before); }

		[operator("begin")] public ArrayIterator<T> Begin() { ArrayIterator<T> it; it.items = data; it.index = 0; return it; }
		[operator("end")] public ArrayIterator<T> End() { ArrayIterator<T> it; it.items = data; it.index = count; return it; }
		[operator("begin")] public ConstArrayIterator<T> Begin() const { ConstArrayIterator<T> it; it.items = data; it.index = 0; return it; }
		[operator("end")] public ConstArrayIterator<T> End() const { ConstArrayIterator<T> it; it.items = data; it.index = count; return it; }

	private:
		private void Reallocate(int64 capacity);   // move the elements into a buffer of exactly `capacity`
		private void Grow(int64 needed);           // at least `needed`, at least double

		private T* data;
		private int64 count;
		private int64 capacity;
	}

	// The layout every Array<T> shares, for code that knows the element type only at run time (Serialization reading
	// through reflection).
	struct ArrayLayout
	{
		public void* data;
		public int64 count;
		public int64 capacity;
	}

	// Gives the empty Array<T> at `array` `count` elements of `elementBytes` aligned to `elementAlign` (sizeof(T) and
	// alignof(T)), stored as Array<T> stores them and zero rather than created: for element types whose zero is a
	// valid value. False when the array is not empty.
	bool ResizeEmptyArray(void* array, int64 count, int64 elementBytes, int64 elementAlign);
