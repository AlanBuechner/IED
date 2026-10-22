#define IMPLEMENT_LOGGER
#include "Core/Core.h"
#include "UnitTesting/Testing.h"
#include "UnitTesting/BenchmarkHistory.h"

// Include all test headers so the reflection pipeline processes them
#include "Tests/Math/ScalarMathTest.h"
#include "Tests/Math/VectorMathTest.h"
#include "Tests/Math/MatrixMathTest.h"
#include "Tests/Math/TransformMathTest.h"
#include "Tests/Math/QuaternionMathTest.h"
#include "Tests/Math/DecompositionMathTest.h"

#include "Tests/Containers/VectorTest.h"
#include "Tests/Containers/DynamicBitAllocatorTest.h"

#include "Tests/String/HashTest.h"
#include "Tests/String/StringFormatTest.h"

#include "Tests/Serialization/JsonDescriptionTest.h"

#include "Tests/Threading/ThreadingTest.h"

#include "Tests/Core/ArgListTest.h"
#include "Tests/Core/BitFlagsTest.h"
#include "Tests/Core/UUIDTest.h"
#include "Tests/Core/TimeTest.h"

#include "Benchmarks/CoreBenchmarks.h"

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
