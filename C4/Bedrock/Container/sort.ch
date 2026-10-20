// Sorting and binary search over contiguous elements, ordered by any callable `before(a, b)` that says
// whether a comes first: a lambda, a funcptr or an object with [operator("call")]. Ascending<T> orders by <.

namespace Core
{
	template<T> struct Ascending {
		[operator("call")] public bool Before(const T& a, const T& b) const { return a < b; }
	}

	// Introsort (std::sort's strategy): quicksort with a depth budget, heapsort when the budget runs out so
	// the worst case stays O(n log n), insertion sort for short ranges. Not stable; elements are relocated,
	// never copied.
	template<T, F> void Sort(T* items, int64 count, F before);

	// The first index whose element is not before `value` (std::lower_bound), or `count`.
	template<T, F> int64 LowerBound(const T* items, int64 count, const T& value, F before);
	// The first index whose element `value` is before (std::upper_bound), or `count`.
	template<T, F> int64 UpperBound(const T* items, int64 count, const T& value, F before);
}
