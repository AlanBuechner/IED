#pragma once
#include "TestBase.h"

namespace Testing
{
	/// Pairs the collected test group root with the owned test instances to delete later.
	struct TestCollection
	{
		TestGroup rootGroup;
		Core::Vector<Test*> instances;
	};

	/// Match a string against a glob-style pattern.
	/// Supports '*' (match any sequence of characters) and '?' (match exactly one character).
	/// Returns true if the entire text matches the pattern.
	bool WildcardMatch(const Core::String& text, const Core::String& pattern);

	/// Recursively build the full qualified path for a test entry within its group hierarchy.
	/// Example: "Math.Scalar.BasicFunctions.TestScalarTrigonometry"
	void BuildTestPath(const Core::String& prefix, const TestEntry& entry, Core::String& outPath);

	/// Collect all discoverable tests from the reflection registry into a hierarchical group.
	/// Returns both the root group (containing test entries with valid instance pointers) and
	/// the vector of owned instances that must be deleted when done.
	TestCollection CollectTestGroup();

	/// Recursively print all tests under a group to std::cout, one per line as full paths.
	void PrintTestPaths(TestGroup& group, const Core::String& prefix);

	/// Recursively run all tests whose full path matches the filter pattern.
	/// If filter is empty, all tests are run. Returns the count of executed tests.
	uint32 RunFilteredTests(TestGroup& group, const Core::String& prefix,
	                        const Core::String& filter, uint32 indent, uint32 nameFieldWidth);

	/// Recursively collect the max test-name length under a group (for column alignment).
	uint32 CollectMaxTestNameLength(TestGroup& group);

}
