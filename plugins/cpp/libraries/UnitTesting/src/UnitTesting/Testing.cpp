#include "Testing.h"
#include "TestFilter.h"
#include "BenchmarkFilter.h"
#include <cstdio>
#include <iostream>
#include <windows.h>

namespace Testing
{

	// --- Static CLI state ------------------------------------------------

	static Core::String s_TestFilter;
	static Core::String s_BenchmarkFilter;

	bool ProcessCliArgs(Core::ArgList& argList)
	{
		bool listOnly = false;

		const Core::Arg* listTests = argList.GetArg("-list-tests");
		if (listTests != nullptr && listTests->value != listTests->name)
		{
			std::cout << "Discoverable tests:" << std::endl;
			TestCollection collection = CollectTestGroup();
			PrintTestPaths(collection.rootGroup, "");
			for (Test* instance : collection.instances)
				delete instance;
			listOnly = true;
		}

		const Core::Arg* listBenchmarks = argList.GetArg("-list-benchmarks");
		if (listBenchmarks != nullptr && listBenchmarks->value != listBenchmarks->name)
		{
			std::cout << "Discoverable benchmarks:" << std::endl;
			BenchmarkCollection collection = CollectBenchmarkGroup();
			PrintBenchmarkPaths(collection.rootGroup, "");
			for (BenchmarkTest* instance : collection.instances)
				delete instance;
			listOnly = true;
		}

		const Core::Arg* filter = argList.GetArg("-filter");
		if (filter != nullptr && filter->value != filter->name)
		{
			s_TestFilter = filter->value;
		}

		const Core::Arg* benchmarkFilter = argList.GetArg("-benchmark-filter");
		if (benchmarkFilter != nullptr && benchmarkFilter->value != benchmarkFilter->name)
		{
			s_BenchmarkFilter = benchmarkFilter->value;
		}

		return listOnly;
	}

	int GetExitCode()
	{
		if (GetFailedTestCount() > 0 || GetFailedBenchmarkCount() > 0)
			return 1;
		return 0;
	}

	void RunTestsAndBenchmarks()
	{
		if (!s_TestFilter.empty())
		{
			TestCollection collection = CollectTestGroup();
			uint32 maxNameLen = CollectMaxTestNameLength(collection.rootGroup);

			// Reset counters before filtered run.
			s_TotalTests = 0;
			s_PassedTests = 0;
			s_FailedTests = 0;

			RunFilteredTests(collection.rootGroup, "", s_TestFilter, 0, maxNameLen);

			std::cout << std::endl;
			// Print filtered summary.
			std::cout << "========================================" << std::endl;
			std::cout << "Test Summary (filtered)" << std::endl;
			std::cout << "  Total:  " << GetTotalTestCount() << std::endl;
			std::cout << "  Passed: " << GetPassedTestCount() << std::endl;
			if (GetFailedTestCount() > 0)
				std::cout << "  Failed: " << GetFailedTestCount() << std::endl;
			else
				std::cout << "  Failed: " << GetFailedTestCount() << std::endl;
			std::cout << "========================================" << std::endl;

			if (GetRunawayTestCount() == 0)
				for (Test* instance : collection.instances)
					delete instance;
		}
		else
		{
			RunTests();
		}

		if (!s_BenchmarkFilter.empty())
		{
			BenchmarkCollection collection = CollectBenchmarkGroup();
			uint32 maxNameLen = CollectMaxBenchmarkNameLength(collection.rootGroup);

			// Reset counters before filtered run.
			s_TotalBenchmarks = 0;
			s_CompletedBenchmarks = 0;
			s_FailedBenchmarks = 0;

			RunFilteredBenchmarks(collection.rootGroup, "", s_BenchmarkFilter, 0, maxNameLen);

			std::cout << std::endl;
			// Print filtered benchmark summary.
			std::cout << "========================================" << std::endl;
			std::cout << "Benchmark Results (filtered)" << std::endl;
			std::cout << "  Total:      " << GetTotalBenchmarkCount() << std::endl;
			std::cout << "  Completed:  " << GetCompletedBenchmarkCount() << std::endl;
			if (GetFailedBenchmarkCount() > 0)
				std::cout << "  Failed:     " << GetFailedBenchmarkCount() << std::endl;
			else
				std::cout << "  Failed:     " << GetFailedBenchmarkCount() << std::endl;
			std::cout << "========================================" << std::endl;

			for (BenchmarkTest* instance : collection.instances)
				delete instance;
		}
		else
		{
			RunBenchmarks();
		}
		EndRunWithoutRunaways();
	}

	void EndRunWithoutRunaways()
	{
		if (GetRunawayTestCount() == 0) return;
		std::cout << GetRunawayTestCount() << " test(s) still running past their timeout: the run ends without them" << std::endl;
		std::cout.flush();
		std::fflush(stdout);
		TerminateProcess(GetCurrentProcess(), 1);
	}

}
