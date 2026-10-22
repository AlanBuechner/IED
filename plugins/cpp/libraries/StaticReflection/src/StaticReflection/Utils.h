#pragma once
#include "Core/Core.h"
#include <queue>

// helper functions
bool isToken(char c, const Core::Vector<char> tokens);

std::queue<Core::String> Tokenize(const Core::String& str,
	const Core::Vector<char> seperatingTokens);

template <typename T> Core::Vector<T> QueueToVector(std::queue<T> q) {
	Core::Vector<T> elems;
	elems.reserve(q.size());
	while (!q.empty()) {
		elems.Push(q.front());
		q.Pop();
	}
	return elems;
}

template <typename T>
bool hasEntry(const Core::Vector<T>& list, const T& entry) {
	for (auto& e : list) {
		if (e == entry)
			return true;
	}
	return false;
}
