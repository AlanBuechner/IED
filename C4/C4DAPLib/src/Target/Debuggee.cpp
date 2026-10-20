#include "Target/Debuggee.h"

namespace C4DAP::Target
{
	namespace
	{
		// While the program runs, calls wait at most this long for the engine to look up from its debug events.
		constexpr DWORD kPollMilliseconds = 20;
	}

	Debuggee::Debuggee(EngineEvents events) : m_Thread(&Debuggee::Loop, this, std::move(events)) {}

	Debuggee::~Debuggee()
	{
		{
			std::lock_guard<std::mutex> lock(m_Lock);
			m_Quit = true;
		}
		m_Wake.notify_one();
		m_Thread.join();
	}

	void Debuggee::Post(std::function<void(Engine&)> work)
	{
		{
			std::lock_guard<std::mutex> lock(m_Lock);
			m_Work.push_back(std::move(work));
		}
		m_Wake.notify_one();
	}

	void Debuggee::Loop(EngineEvents events)
	{
		Engine engine(std::move(events)); // ended on this thread, which owns the program
		while (true)
		{
			std::deque<std::function<void(Engine&)>> batch;
			{
				std::unique_lock<std::mutex> lock(m_Lock);
				if (!engine.Running()) m_Wake.wait(lock, [this] { return m_Quit || !m_Work.empty(); });
				if (m_Quit && m_Work.empty()) break;
				batch.swap(m_Work);
			}
			for (auto& work : batch) work(engine);
			if (engine.Running()) engine.Poll(kPollMilliseconds);
		}
	}
} // namespace C4DAP::Target
