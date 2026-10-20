#pragma once
#include "BenchmarkTest.h"

namespace Testing
{
	/// Pairs the collected benchmark group root with the owned benchmark instances to delete later.
	struct BenchmarkCollection
	{
		BenchmarkGroup rootGroup;
		Core::Vector<BenchmarkTest*> instances;
	};

	/// Recursively build the full qualified path for a benchmark entry within its group hierarchy.
	/// Example: "Benchmarks.Vector.BenchVectorPushBack"
	void BuildBenchmarkPath(const Core::String& prefix, const BenchmarkEntry& entry, Core::String& outPath);

	/// Collect all discoverable benchmarks from the reflection registry into a hierarchical group.
	BenchmarkCollection CollectBenchmarkGroup();

	/// Recursively print all benchmarks under a group to std::cout, one per line as full paths.
	void PrintBenchmarkPaths(BenchmarkGroup& group, const Core::String& prefix);

	/// Recursively collect the max benchmark-name length under a group (for column alignment).
	uint32 CollectMaxBenchmarkNameLength(BenchmarkGroup& group);

	/// Recursively run all benchmarks whose full path matches the filter pattern.
	/// If filter is empty, all benchmarks are run. Returns the count of executed benchmarks.
	uint32 RunFilteredBenchmarks(BenchmarkGroup& group, const Core::String& prefix,
	                             const Core::String& filter, uint32 indent, uint32 nameFieldWidth);

	/// Increment the global total benchmark count (for summary reporting in filtered runs).
	void IncrementTotalBenchmarkCount();
}
