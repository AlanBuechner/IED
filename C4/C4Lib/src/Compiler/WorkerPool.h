#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Scripting
{
	/// Threads kept for many short rounds of parallel work, where ParallelFor would start its own each round: Run calls
	/// `body(i)` for each i below `count` on the pool's threads and the calling one, each taking the next index as it
	/// finishes one, and returns when every call has returned. `body` must not throw.
	class WorkerPool
	{
	public:
		/// `threads` counts the calling thread: a pool of one runs everything on it.
		explicit WorkerPool(unsigned threads)
		{
			for (unsigned i = 1; i < threads; ++i) m_Workers.emplace_back([this] { Work(); });
		}

		~WorkerPool()
		{
			{
				const std::lock_guard<std::mutex> held(m_Lock);
				m_Stop = true;
			}
			m_Start.notify_all();
			for (std::thread& worker : m_Workers) worker.join();
		}

		WorkerPool(const WorkerPool&) = delete;
		WorkerPool& operator=(const WorkerPool&) = delete;

		void Run(size_t count, std::function<void(size_t)> body)
		{
			{
				const std::lock_guard<std::mutex> held(m_Lock);
				m_Body = std::move(body);
				m_Count = count;
				m_Next = 0;
				m_Busy = static_cast<unsigned>(m_Workers.size());
				++m_Round;
			}
			m_Start.notify_all();
			Drain();
			std::unique_lock<std::mutex> held(m_Lock);
			m_Done.wait(held, [this] { return m_Busy == 0; });
		}

	private:
		void Drain()
		{
			for (size_t i = m_Next++; i < m_Count; i = m_Next++) m_Body(i);
		}

		void Work()
		{
			uint64_t seen = 0;
			while (true)
			{
				{
					std::unique_lock<std::mutex> held(m_Lock);
					m_Start.wait(held, [&] { return m_Stop || m_Round != seen; });
					if (m_Stop) return;
					seen = m_Round;
				}
				Drain();
				const std::lock_guard<std::mutex> held(m_Lock);
				if (--m_Busy == 0) m_Done.notify_one();
			}
		}

		std::vector<std::thread> m_Workers;
		std::mutex m_Lock;
		std::condition_variable m_Start; // a round began, or the pool ends
		std::condition_variable m_Done;  // every worker finished the round
		std::function<void(size_t)> m_Body;
		size_t m_Count = 0;
		std::atomic<size_t> m_Next = 0;
		uint64_t m_Round = 0;
		unsigned m_Busy = 0; // workers not done with this round
		bool m_Stop = false;
	};
} // namespace Scripting
