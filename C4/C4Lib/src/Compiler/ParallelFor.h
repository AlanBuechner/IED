#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace Scripting
{
	/// `body(i)` for each i below `count`, on up to `threads` threads (the calling one among them), each taking the next
	/// index as it finishes one: for work whose items take very different times.
	template <typename Body>
	void ParallelFor(size_t count, unsigned threads, Body&& body)
	{
		std::atomic<size_t> next = 0;
		auto work = [&] {
			for (size_t i = next++; i < count; i = next++) body(i);
		};
		std::vector<std::thread> workers;
		const size_t workerCount = std::min<size_t>(std::max(threads, 1u), count);
		for (size_t i = 1; i < workerCount; ++i) workers.emplace_back(work);
		work();
		for (std::thread& worker : workers) worker.join();
	}
} // namespace Scripting
