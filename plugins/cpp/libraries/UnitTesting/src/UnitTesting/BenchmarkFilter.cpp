#include "BenchmarkFilter.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

// Include TestFilter for WildcardMatch (shared utility).
#include "TestFilter.h"
#include "CrashHandler.h"

namespace Testing
{

	// ─── Benchmark path building ────────────────────────────────────────

	void BuildBenchmarkPath(const Core::String& prefix, const BenchmarkEntry& entry, Core::String& outPath)
	{
		if (!prefix.empty())
			outPath = prefix + ".";
		else
			outPath.clear();

		outPath += entry.func->GetName();
	}

	// ─── Collect benchmark group ────────────────────────────────────────

	BenchmarkCollection CollectBenchmarkGroup()
	{
		const std::vector<const Reflect::Class*>& benchmarkClasses =
			Reflect::Registry::GetRegistry()->GetGroup("Benchmarking");

		BenchmarkCollection collection;
		collection.rootGroup.name = "All Benchmarks";

		for (const Reflect::Class* benchmarkClass : benchmarkClasses)
		{
			BenchmarkTest* instance = static_cast<BenchmarkTest*>(benchmarkClass->CreateInstance());
			collection.rootGroup.RegisterBenchmarks(*benchmarkClass, instance);
			collection.instances.Push(instance);
		}

		return collection;
	}

	// ─── Print benchmark paths ──────────────────────────────────────────

	void PrintBenchmarkPaths(BenchmarkGroup& group, const Core::String& prefix)
	{
		for (const BenchmarkEntry& entry : group.benchmarks)
		{
			Core::String path;
			BuildBenchmarkPath(prefix, entry, path);
			std::cout << path << std::endl;
		}

		for (BenchmarkGroup& subGroup : group.subGroups)
		{
			Core::String sgPrefix = prefix.empty() ? subGroup.name : prefix + "." + subGroup.name;
			PrintBenchmarkPaths(subGroup, sgPrefix);
		}
	}

	// ─── Helpers for aligned output ─────────────────────────────────────

	uint32 CollectMaxBenchmarkNameLength(BenchmarkGroup& group)
	{
		uint32 maxLen = 0;
		for (const BenchmarkEntry& entry : group.benchmarks)
			maxLen = std::max(maxLen, static_cast<uint32>(entry.func->GetName().length()));

		for (BenchmarkGroup& subGroup : group.subGroups)
			maxLen = std::max(maxLen, CollectMaxBenchmarkNameLength(subGroup));

		return maxLen;
	}

	// ─── Recursive match checker for benchmarks ─────────────────────────

	static bool BenchmarkGroupHasMatch(BenchmarkGroup& g, const Core::String& gPrefix, const Core::String& filter)
	{
		for (const BenchmarkEntry& entry : g.benchmarks)
		{
			Core::String path;
			BuildBenchmarkPath(gPrefix, entry, path);
			if (WildcardMatch(path, filter)) return true;
		}
		for (BenchmarkGroup& sg : g.subGroups)
		{
			Core::String sgPrefix = gPrefix.empty() ? sg.name : gPrefix + "." + sg.name;
			if (BenchmarkGroupHasMatch(sg, sgPrefix, filter)) return true;
		}
		return false;
	}

	// ─── Filtered benchmark runner ──────────────────────────────────────

	void IncrementTotalBenchmarkCount()
	{
		++s_TotalBenchmarks;
	}

	uint32 RunFilteredBenchmarks(BenchmarkGroup& group, const Core::String& prefix,
	                             const Core::String& filter, uint32 indent, uint32 nameFieldWidth)
	{
		uint32 executedCount = 0;

		// If filter is empty, run all benchmarks (no filtering).
		bool filterActive = !filter.empty();

		// When filter is active, check if this group or any subgroup has matching benchmarks.
		if (filterActive && !prefix.empty())
		{
			bool anyMatch = BenchmarkGroupHasMatch(group, prefix, filter);
			if (!anyMatch)
				return 0;
		}

		for (BenchmarkEntry& entry : group.benchmarks)
		{
			Core::String path;
			BuildBenchmarkPath(prefix, entry, path);

			if (filterActive && !WildcardMatch(path, filter))
				continue;

		executedCount++;
		IncrementTotalBenchmarkCount();

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
			s_FailedBenchmarks++;
			Logging::Logger::SetConsoleColor(Logging::Logger::Error);

			switch (result)
			{
			case InvocationResult::SehCrash:
				if (!crashInfo.description.empty())
					std::cout << "Crashed: SEH " << crashInfo.description << std::endl;
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
			break;
		}
		}

		for (BenchmarkGroup& subGroup : group.subGroups)
		{
			Core::String sgPrefix = prefix.empty() ? subGroup.name : prefix + "." + subGroup.name;
			executedCount += RunFilteredBenchmarks(subGroup, sgPrefix, filter, indent + 1, nameFieldWidth);
		}

		return executedCount;
	}

} // namespace Testing
