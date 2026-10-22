#pragma once
#include "Core/Common.h"
#include <string>
#include "Core/Types.h"

// Profiling through the IED Trace Protocol client (Profiling/Itp.h, IED docs/16): with ENABLE_PROFILING a program
// streams its zones to the IED profiler when run with IED_PROFILER set (the Profile task sets it); without it the
// macros compile to nothing.

#if !defined(ENABLE_PROFILING)
#define ENABLE_PROFILING 0
#endif

#include "Profiling/Itp.h"

namespace Profiler
{
	class Instrumentor
	{
	public:
		Instrumentor(bool record = false) : m_RecordData(record) {}
		void RecordData(bool record) { m_RecordData = record; }
		// Names the calling thread in the profiler (again whenever it takes another role); `order` is kept for callers.
		void RegisterThread(const std::string& name, uint32 order = 0);
		static Instrumentor& Get();
	private:
		bool m_RecordData;
	};
}

#if ENABLE_PROFILING
#define FRAME_MARK itp_frame(nullptr)
#define SOURCE_LOC_NAME ITP_CONCAT(__profileSite, __LINE__)
#define CREATE_SOURCE_LOC(name) static itp_site SOURCE_LOC_NAME = { name, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0, 0, 0 }
#define CREATE_PROFILE_SCOPEI(name) CREATE_SOURCE_LOC(name); itp::Zone __timer(&SOURCE_LOC_NAME)
#define CREATE_PROFILE_FUNCTIONI() CREATE_PROFILE_SCOPEI(__FUNCTION__)
#define ANNOTATE_PROFILEI(anotation) __timer.Text(anotation)
#else
#define FRAME_MARK
#define SOURCE_LOC_NAME
#define CREATE_SOURCE_LOC(...)
#define CREATE_PROFILE_SCOPEI(...)
#define CREATE_PROFILE_FUNCTIONI(...)
#define ANNOTATE_PROFILEI(...)
#endif
