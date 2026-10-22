#pragma once

#include "Reflection.h"
#include "Core/Core.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/BenchmarkRunner.h"
#include <exception>

#define BENCHMARK_FUNC(group) FUNCTION(BenchmarkGroup=group)
#define BENCHMARK_PASSED(result) { BenchmarkTest::BenchmarkPassed(result); return; }
#define BENCHMARK_FAILED(...) { BenchmarkTest::BenchmarkFailed(__VA_ARGS__); throw BenchmarkFailedException(#__VA_ARGS__); }

namespace Testing
{
	/// A single registered benchmark - pairs the reflected function with its owning instance.
	struct BenchmarkEntry
	{
		const Reflect::Function* func;
		void* ownerInstance;
	};

	/// Hierarchical grouping of benchmarks, mirroring TestGroup for unit tests.
	struct BenchmarkGroup
	{
		Core::String name;
		Core::Vector<BenchmarkGroup> subGroups;
		Core::Vector<BenchmarkEntry> benchmarks;

		BenchmarkGroup* GetOrCreateSubGroup(const Core::String& name);

		/// Registers the benchmark functions of one reflected class into this group.
		/// Each function tagged with BENCHMARK_FUNC becomes a leaf under its hierarchical group path.
		void RegisterBenchmarks(const Reflect::Class& benchmarkClass, void* ownerInstance);
	};

	/// Collects all registered benchmark functions from every reflected benchmark class into
	/// a single hierarchical BenchmarkGroup structure, then runs them via BenchmarkRunner.
	void RunBenchmarks();

	/// Get the collected benchmark results from the last RunBenchmarks() call.
	const Core::Vector<Core::BenchmarkResult>& GetBenchmarkResults();

	/// Returns the number of failed benchmarks from the last run (full or filtered).
	uint32 GetFailedBenchmarkCount();

	/// Returns the total number of benchmarks executed in the last run (full or filtered).
	uint32 GetTotalBenchmarkCount();

	/// Returns the number of completed benchmarks from the last run (full or filtered).
	uint32 GetCompletedBenchmarkCount();

	/// Exception thrown when a benchmark fails. Caught by the benchmark runner to stop
	/// the current benchmark immediately and prevent running code on invalid state.
	class BenchmarkFailedException : public std::exception
	{
	public:
		explicit BenchmarkFailedException(const Core::String& msg) : m_Msg(msg) {}
		const char* what() const noexcept override { return m_Msg.c_str(); }
	private:
		Core::String m_Msg;
	};

	/// Base class for benchmark test suites.  Inherit from this, tag methods with BENCHMARK_FUNC,
	/// and call BENCHMARK_PASSED(result) or BENCHMARK_FAILED(...) from each method.
	class BenchmarkTest : public Reflect::Reflected
	{
	public:
		virtual ~BenchmarkTest() = default;

	protected:

		static void BenchmarkPassed(const Core::BenchmarkResult& result);

		template<typename ... Args>
		void BenchmarkFailed(const Core::String& msg = "", const Args&... args) {
			BenchmarkFailedI(Core::StringFormat::Format(msg.c_str(), args...));
		}

	private:
		static void BenchmarkFailedI(const Core::String& msg);
	};

	// Shared benchmark result counters — defined in BenchmarkTest.cpp, used by filter runners.
	extern uint32 s_FailedBenchmarks;
	extern uint32 s_CompletedBenchmarks;
	extern uint32 s_TotalBenchmarks;
}
