#pragma once
#include "TestBase.h"
#include "Core/ArgList.h"

namespace Testing
{
	/// Parse CLI arguments for test/benchmark filtering and enumeration.
	/// Supported flags:
	///   --list-tests / -list-tests          Print all discoverable test paths and return true (skip execution).
	///   --list-benchmarks / -list-benchmarks  Print all discoverable benchmark paths and return true (skip execution).
	///   --filter=<pattern>                   Run only tests matching the wildcard pattern.
	///   --benchmark-filter=<pattern>         Run only benchmarks matching the wildcard pattern.
	/// Returns true if a "list" flag was set (caller should exit without running tests/benchmarks).
	bool ProcessCliArgs(Core::ArgList& argList);

	/// Returns 0 if all tests and benchmarks passed, 1 if any failed.
	int GetExitCode();

	/// Runs all unit tests followed by all benchmarks in a single call.
	/// Respects --filter and --benchmark-filter patterns set via ProcessCliArgs.
	void RunTestsAndBenchmarks();
}
