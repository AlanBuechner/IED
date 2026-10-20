#pragma once
#include <map>
#include <set>

namespace Core
{
	template<typename T, typename T2>
	using Map = std::map<T, T2>;

	template<typename T>
	using Set = std::set<T>;
}