// Sorting numbers by their bits: an LSD radix sort, a byte a pass, in time linear in the count (std::sort compares
// n log n times). AscendingSorter<T> is what Array::Sort and SortAscending use: pdqsort by < for any T, and this
// radix sort for the integers, characters and floats, whose keys order as their values do. Short ranges go to
// insertion and pdqsort, where a radix sort's fixed passes cost more than they save.

include "sort.ch"

namespace Core
{
	// A number's key: unsigned bits that order as the number does (a signed integer's sign bit flipped; a float's bits
	// all flipped when negative, its sign bit flipped when not, so -0 comes before +0 and the NaNs at the ends).
	template<T> struct RadixKey { }
	template<> struct RadixKey<int8> { public static uint64 Of(int8 v) { return (uint64)((uint8)v ^ 128u); } public static int32 Bytes() { return 1; } }
	template<> struct RadixKey<uint8> { public static uint64 Of(uint8 v) { return (uint64)v; } public static int32 Bytes() { return 1; } }
	template<> struct RadixKey<char8> { public static uint64 Of(char8 v) { return (uint64)(uint8)v; } public static int32 Bytes() { return 1; } }
	template<> struct RadixKey<int16> { public static uint64 Of(int16 v) { return (uint64)((uint16)v ^ 32768u); } public static int32 Bytes() { return 2; } }
	template<> struct RadixKey<uint16> { public static uint64 Of(uint16 v) { return (uint64)v; } public static int32 Bytes() { return 2; } }
	template<> struct RadixKey<int32> { public static uint64 Of(int32 v) { return (uint64)((uint32)v ^ 2147483648u); } public static int32 Bytes() { return 4; } }
	template<> struct RadixKey<uint32> { public static uint64 Of(uint32 v) { return (uint64)v; } public static int32 Bytes() { return 4; } }
	template<> struct RadixKey<int64> { public static uint64 Of(int64 v) { return (uint64)v ^ 9223372036854775808u; } public static int32 Bytes() { return 8; } }
	template<> struct RadixKey<uint64> { public static uint64 Of(uint64 v) { return v; } public static int32 Bytes() { return 8; } }
	template<> struct RadixKey<float> {
		public static uint64 Of(float v) { uint32 bits = reinterpret_cast<uint32>(v); return (uint64)((bits & 2147483648u) != 0u ? ~bits : bits ^ 2147483648u); }
		public static int32 Bytes() { return 4; }
	}
	template<> struct RadixKey<double> {
		public static uint64 Of(double v) { uint64 bits = reinterpret_cast<uint64>(v); return (bits >> 63) != 0u ? ~bits : bits ^ 9223372036854775808u; }
		public static int32 Bytes() { return 8; }
	}

	// `count` numbers in ascending order of their RadixKey.
	template<T> void RadixSort(T* items, int64 count);

	// Ascending order by <: pdqsort for any T, the radix sort for numbers.
	template<T> struct AscendingSorter { public static void Sort(T* items, int64 count) { Core::Sort(items, count, Ascending<T>()); } }
	template<> struct AscendingSorter<int8> { public static void Sort(int8* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<uint8> { public static void Sort(uint8* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<char8> { public static void Sort(char8* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<int16> { public static void Sort(int16* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<uint16> { public static void Sort(uint16* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<int32> { public static void Sort(int32* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<uint32> { public static void Sort(uint32* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<int64> { public static void Sort(int64* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<uint64> { public static void Sort(uint64* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<float> { public static void Sort(float* items, int64 count) { RadixSort(items, count); } }
	template<> struct AscendingSorter<double> { public static void Sort(double* items, int64 count) { RadixSort(items, count); } }

	// `count` elements in ascending order by <, the fastest way there is for T.
	template<T> void SortAscending(T* items, int64 count) { AscendingSorter<T>::Sort(items, count); }
}
