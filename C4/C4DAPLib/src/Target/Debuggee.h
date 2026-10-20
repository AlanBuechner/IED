#pragma once
#include "Target/Engine.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace C4DAP::Target
{
	/// The engine on a thread of its own (the one Windows sends the program's debug events to). Calls run there one
	/// at a time and the caller waits for the answer; while the program runs, the thread waits for its debug events
	/// between calls. The engine's events are reported on that thread too.
	class Debuggee
	{
	public:
		explicit Debuggee(EngineEvents events);
		~Debuggee(); // ends the program, then the thread
		Debuggee(const Debuggee&) = delete;
		Debuggee& operator=(const Debuggee&) = delete;

		template <typename Work>
		auto Call(Work work) -> decltype(work(std::declval<Engine&>()))
		{
			using Result = decltype(work(std::declval<Engine&>()));
			auto task = std::make_shared<std::packaged_task<Result(Engine&)>>(std::move(work));
			std::future<Result> answer = task->get_future();
			Post([task](Engine& engine) { (*task)(engine); });
			return answer.get();
		}

	private:
		void Post(std::function<void(Engine&)> work);
		void Loop(EngineEvents events);

		std::mutex m_Lock;
		std::condition_variable m_Wake;
		std::deque<std::function<void(Engine&)>> m_Work;
		bool m_Quit = false;
		std::thread m_Thread;
	};
} // namespace C4DAP::Target
