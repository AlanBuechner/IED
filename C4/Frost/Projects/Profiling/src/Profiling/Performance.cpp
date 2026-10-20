#include "Profiling/MemTracker/MemTracker.h"
#include "Profiling/Performance.h"
#include <algorithm>

namespace Profiler
{

	void Instrumentor::RegisterThread(const std::string& name, uint32 order)
	{
		//uint32 threadID = (uint32)std::hash<std::thread::id>{}(std::this_thread::get_id());
#if ENABLE_PROFILING
		tracy::SetThreadNameWithHint(name.c_str(), order);
#endif

	}

	Instrumentor& Instrumentor::Get()
	{
		static Instrumentor* instance = new Instrumentor();
		return *instance;
	}

}
