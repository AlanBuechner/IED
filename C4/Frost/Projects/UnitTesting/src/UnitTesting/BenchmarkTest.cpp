#include "BenchmarkTest.h"
#include "BenchmarkHistory.h"
#include <string>
#include <vector>
#include <sstream>
#include "CrashHandler.h"

namespace Testing
{

	// ─── File-local benchmark result counters ──────────────────────────────

	uint32 s_TotalBenchmarks = 0;
	uint32 s_CompletedBenchmarks = 0;
	uint32 s_FailedBenchmarks = 0;
	Core::Vector<Core::BenchmarkResult> s_BenchmarkResults;


	// ─── BenchmarkGroup ────────────────────────────────────────────────────

	Testing::BenchmarkGroup* BenchmarkGroup::GetOrCreateSubGroup(const Core::String& name)
	{
		for (uint32 i = 0; i < subGroups.Count(); i++) {
			if (subGroups[i].name == name)
				return &subGroups[i];
		}

		subGroups.Push({});
		subGroups.Back().name = name;
		return &subGroups.Back();
	}

	void BenchmarkGroup::RegisterBenchmarks(const Reflect::Class& benchmarkClass, void* ownerInstance)
	{
		const std::vector<Reflect::Function>& funcs = benchmarkClass.GetFunctions();

		for (const Reflect::Function& func : funcs)
		{
			if (!func.HasAttribute("BenchmarkGroup"))
				continue;

			const Reflect::Attribute& benchmarkGroupAttrib = func.GetAttribute("BenchmarkGroup");
			std::stringstream ss(benchmarkGroupAttrib.GetValue());
			Core::String segment;
			Core::Vector<Core::String> groupNames;

			while (std::getline(ss, segment, '.'))
				groupNames.Push(segment);

			BenchmarkGroup* group = this;
			for (const Core::String& groupName : groupNames)
				group = group->GetOrCreateSubGroup(groupName);

			group->benchmarks.Push({});
			group->benchmarks.Back().func = &func;
			group->benchmarks.Back().ownerInstance = ownerInstance;
		}
	}

	// ─── Helpers for aligned output ────────────────────────────────────────

	static uint32 CollectMaxBenchmarkNameLength(BenchmarkGroup& group)
	{
		uint32 maxLen = 0;
		for (BenchmarkEntry& entry : group.benchmarks)
			maxLen = std::max(maxLen, static_cast<uint32>(entry.func->GetName().length()));

		for (BenchmarkGroup& subGroup : group.subGroups)
			maxLen = std::max(maxLen, CollectMaxBenchmarkNameLength(subGroup));

		return maxLen;
	}

	static void ReportBenchmarkCrash(InvocationResult result, const CrashInfo* crashInfo)
	{
		s_FailedBenchmarks++;
		Logging::Logger::SetConsoleColor(Logging::Logger::Error);

		switch (result)
		{
		case InvocationResult::SehCrash:
			if (crashInfo && !crashInfo->description.empty())
				std::cout << "Crashed: SEH " << crashInfo->description << std::endl;
			else
				std::cout << "Crashed: SEH exception" << std::endl;
			break;
		case InvocationResult::StdException:
			std::cout << "Crashed: unhandled standard exception" << std::endl;
			break;
		case InvocationResult::UnknownException:
			std::cout << "Crashed: unknown exception" << std::endl;
			break;
		default:
			std::cout << "Failed" << std::endl;
			break;
		}

		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
	}

	static void RunBenchmarkGroup(BenchmarkGroup& group, uint32 indent, uint32 nameFieldWidth)
	{
		for (BenchmarkEntry& entry : group.benchmarks)
		{
			s_TotalBenchmarks++;

			// Invoke the benchmark method on its owning instance.
			// Use crash handler to catch SEH exceptions and C++ exceptions without disrupting the suite.
			CrashInfo crashInfo;
			auto result = InvokeWithCrashHandling([&entry]() {
				entry.func->Invoke(entry.ownerInstance, {});
			}, &crashInfo);

			switch (result)
			{
			case InvocationResult::Success:
				// Benchmark completed normally — it should have called BENCHMARK_PASSED().
				break;
			case InvocationResult::TestFailed:
				// Failure already reported by the benchmark macro; just skip BENCHMARK_PASSED().
				break;
			default:
				ReportBenchmarkCrash(result, &crashInfo);
				break;
			}
		}

		for (BenchmarkGroup& subGroup : group.subGroups)
			RunBenchmarkGroup(subGroup, indent + 1, nameFieldWidth);
	}

	void BenchmarkTest::BenchmarkPassed(const Core::BenchmarkResult& result)
	{
		s_CompletedBenchmarks++;
		s_BenchmarkResults.Push(result);
	}

	void BenchmarkTest::BenchmarkFailedI(const Core::String& msg)
	{
		s_FailedBenchmarks++;
		Logging::Logger::SetConsoleColor(Logging::Logger::Error);
		std::cout << "Failed: " << msg << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
	}

	// ─── Global entry point ────────────────────────────────────────────────

	void RunBenchmarks()
	{
		const std::vector<const Reflect::Class*>& benchmarkClasses =
			Reflect::Registry::GetRegistry()->GetGroup("Benchmarking");

		// Single root group that accumulates every benchmark class.
		BenchmarkGroup rootGroup;
		rootGroup.name = "All Benchmarks";

		// Phase 1: Collect all benchmarks from every reflected benchmark class.
		// Keep each instance alive so benchmark methods have a valid 'this' pointer.
		std::vector<BenchmarkTest*> instances;

		for (const Reflect::Class* benchmarkClass : benchmarkClasses)
		{
			BenchmarkTest* instance = static_cast<BenchmarkTest*>(benchmarkClass->CreateInstance());
			rootGroup.RegisterBenchmarks(*benchmarkClass, instance);
			instances.push_back(instance);
		}

		// Phase 2: Run all collected benchmarks.
		s_TotalBenchmarks = 0;
		s_CompletedBenchmarks = 0;
		s_FailedBenchmarks = 0;
		s_BenchmarkResults.Clear();

		uint32 maxNameLen = CollectMaxBenchmarkNameLength(rootGroup);
		RunBenchmarkGroup(rootGroup, 0, maxNameLen);

		// Phase 3: Print formatted summary table.
		std::cout << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Info);
		std::cout << "========================================" << std::endl;
		std::cout << "Benchmark Results" << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
		std::cout << "  Total:      " << s_TotalBenchmarks << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Info);
		std::cout << "  Completed:  " << s_CompletedBenchmarks << std::endl;

		if (s_FailedBenchmarks > 0) {
			Logging::Logger::SetConsoleColor(Logging::Logger::Error);
			std::cout << "  Failed:     " << s_FailedBenchmarks << std::endl;
		} else {
			std::cout << "  Failed:     " << s_FailedBenchmarks << std::endl;
		}

		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);

		// Phase 4: Print results table.
		if (s_BenchmarkResults.Count() > 0)
		{
			std::cout << std::endl;
			Logging::Logger::SetConsoleColor(Logging::Logger::Info);

			// Header row
			std::cout << std::left << std::setw(24) << "Name"
				<< std::right << std::setw(10) << "Mean(s)"
				<< std::setw(12) << "Median(s)"
				<< std::setw(12) << "Min(s)"
				<< std::setw(12) << "Max(s)"
				<< std::setw(10) << "Iters"
				<< std::endl;

			Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
			for (const Core::BenchmarkResult& result : s_BenchmarkResults)
			{
				std::cout << std::left << std::setw(24) << result.name
					<< std::right << std::fixed << std::setprecision(6)
					<< std::setw(10) << result.stats.mean
					<< std::setw(12) << result.stats.median
					<< std::setw(12) << result.stats.minTime
					<< std::setw(12) << result.stats.maxTime
					<< std::setw(10) << result.stats.iterations
					<< std::endl;
			}
		}

		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
		std::cout << "========================================" << std::endl;

		// Clean up instances.
		for (BenchmarkTest* instance : instances)
			delete instance;
	}

	const Core::Vector<Core::BenchmarkResult>& GetBenchmarkResults()
	{
		return s_BenchmarkResults;
	}

	uint32 GetFailedBenchmarkCount()
	{
		return s_FailedBenchmarks;
	}

	uint32 GetTotalBenchmarkCount()
	{
		return s_TotalBenchmarks;
	}

	uint32 GetCompletedBenchmarkCount()
	{
		return s_CompletedBenchmarks;
	}

}
