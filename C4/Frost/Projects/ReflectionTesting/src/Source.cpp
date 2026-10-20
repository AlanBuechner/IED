#define IMPLEMENT_LOGGER
#include "Core/Core.h"
#include "UnitTesting/Testing.h"
#include "UnitTesting/BenchmarkHistory.h"

// Include all test headers so the reflection pipeline processes them
#include "Tests/ScriptingIDTest.h"
#include "Tests/CallbackTest.h"
#include "Tests/ReloadTest.h"
#include "Tests/StaticReflectionTest.h"
#include "Tests/ChangeNotificationTest.h"

int main(int argc, char* argv[])
{
	// Build ArgList from command-line arguments.
	Core::Vector<Core::String> args;
	for (int i = 1; i < argc; i++)
		args.Push(argv[i]);

	Core::ArgList argList(args);

	// Process --filter, --benchmark-filter, --list-tests, and --list-benchmarks flags.
	bool listOnly = Testing::ProcessCliArgs(argList);
	if (listOnly)
		return 0;

	Testing::RunTestsAndBenchmarks();

	return Testing::GetExitCode();
}
