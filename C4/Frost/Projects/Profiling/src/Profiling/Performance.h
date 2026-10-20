#pragma once
#include "Core/Common.h"
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <unordered_map>
#include "Core/Types.h"
#include "Core/Time.h"

#if !defined(ENABLE_PROFILING)
#define ENABLE_PROFILING 0
#endif

#if ENABLE_PROFILING

#define TRACY_ENABLE
#include "tracy/Tracy.hpp"

#endif


namespace Profiler
{

	// ---------------------- Instrumentor ---------------------- //
	class Instrumentor
	{
	public:
		Instrumentor(bool record = false)
			: m_CurrentSessionName(""), m_RecordData(record)
		{}

		void RecordData(bool record) { m_RecordData = record; }

		void RegisterThread(const std::string& name, uint32 order = 0);

		static Instrumentor& Get();

	private:
		std::string m_CurrentSessionName;
		bool m_RecordData;
	};


#pragma region InstrumentationTimers
#if ENABLE_PROFILING

	class InstrumentationTimer
	{
	public:
		inline void Start(const tracy::SourceLocationData& sourceLocationData);
		inline void End();


	private:
	};

	class InstrumentationTimerScoped
	{
	public:
		InstrumentationTimerScoped(const tracy::SourceLocationData* sourceLocationData) :
			m_ScopedZone(sourceLocationData, TRACY_CALLSTACK, true)
		{ }

		~InstrumentationTimerScoped()
		{ }

		inline void Anotate(const std::string& anotation) {
			m_ScopedZone.Text(anotation.c_str(), anotation.size());
		}

	private:
		tracy::ScopedZone m_ScopedZone;
	};

#endif
#pragma endregion

}


#if ENABLE_PROFILING

#define FRAME_MARK FrameMark

#define SOURCE_LOC_NAME TracyConcat(__tracy_source_location,TracyLine)
#define CREATE_SOURCE_LOC(name) static constexpr tracy::SourceLocationData SOURCE_LOC_NAME { name, __FUNCTION__,  __FILE__, (uint32_t)TracyLine, 0 };
#define CREATE_PROFILE_SCOPEI(name) CREATE_SOURCE_LOC(name); Profiler::InstrumentationTimerScoped __timer(&SOURCE_LOC_NAME);
#define CREATE_PROFILE_FUNCTIONI() CREATE_PROFILE_SCOPEI(__FUNCSIG__)

#define ANNOTATE_PROFILEI(anotation) __timer.Anotate(anotation);

#else

#define FRAME_MARK

#define SOURCE_LOC_NAME
#define CREATE_SOURCE_LOC(...)
#define CREATE_PROFILE_SCOPEI(...)
#define CREATE_PROFILE_FUNCTIONI(...)

#define ANNOTATE_PROFILEI(...)

#endif
