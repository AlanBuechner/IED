// InitList<T>: the public name of the built-in braced-list view __init_list<T> (std::initializer_list, D-53):
// `data`, `size`, indexing and range-for over a list the caller built. A parameter of this type takes a
// braced list:
//     int32 Sum(Core::InitList<int32> values) { int32 s = 0; for (int32 v : values) s += v; return s; }
//     Sum({ 1, 2, 3 });

namespace Core
{
	template<T> alias InitList = __init_list<T>;
}
