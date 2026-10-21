#include "Profiling/MemTracker/MemTracker.h"
#include "Profiling/Performance.h"

namespace Profiler
{
	void Instrumentor::RegisterThread(const std::string& name, uint32)
	{
#if ENABLE_PROFILING
		itp_thread_name(name.c_str());
#else
		(void)name;
#endif
	}

	Instrumentor& Instrumentor::Get()
	{
		static Instrumentor* instance = new Instrumentor();
		return *instance;
	}
}
