#include "TestFilter.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>
#include "CrashHandler.h"

namespace Testing
{

	// ─── Wildcard pattern matching ──────────────────────────────────────

	bool WildcardMatch(const Core::String& text, const Core::String& pattern)
	{
		size_t ti = 0, pi = 0;
		size_t starTi = std::string::npos, starPi = std::string::npos;

		while (ti < text.size())
		{
			char tc = text[ti];
			char pc = (pi < pattern.size()) ? pattern[pi] : '\0';

			if (pc == tc || pc == '?')
			{
				// Exact or single-char wildcard match.
				ti++;
				pi++;
			}
			else if (pc == '*')
			{
				// Record star position and advance pattern; text cursor stays.
				starTi = ti;
				starPi = pi + 1;
				pi = starPi;
			}
			else if (starPi != std::string::npos)
			{
				// Mismatch — try extending the '*' match by one more text char.
				ti = ++starTi;
				pi = starPi;
			}
			else
			{
				return false; // No '*' to fall back on.
			}
		}

		// Consume trailing '*' characters in the pattern.
		while (pi < pattern.size() && pattern[pi] == '*')
			pi++;

		return pi == pattern.size();
	}

	// ─── Test path building ──────────────────────────────────────────────

	void BuildTestPath(const Core::String& prefix, const TestEntry& entry, Core::String& outPath)
	{
		if (!prefix.empty())
			outPath = prefix + ".";
		else
			outPath.clear();

		outPath += entry.func->GetName();
	}

	// ─── Collect test group ──────────────────────────────────────────────

	TestCollection CollectTestGroup()
	{
		const std::vector<const Reflect::Class*>& testClasses =
			Reflect::Registry::GetRegistry()->GetGroup("Testing");

		TestCollection collection;
		collection.rootGroup.name = "All Tests";

		for (const Reflect::Class* testClass : testClasses)
		{
			Test* instance = static_cast<Test*>(testClass->CreateInstance());
			collection.rootGroup.RegisterTests(*testClass, instance);
			collection.instances.Push(instance);
		}

		return collection;
	}

	// ─── Print test paths ────────────────────────────────────────────────

	void PrintTestPaths(TestGroup& group, const Core::String& prefix)
	{
		for (const TestEntry& entry : group.tests)
		{
			Core::String path;
			BuildTestPath(prefix, entry, path);
			std::cout << path << std::endl;
		}

		for (TestGroup& subGroup : group.subGroups)
		{
			Core::String sgPrefix = prefix.empty() ? subGroup.name : prefix + "." + subGroup.name;
			PrintTestPaths(subGroup, sgPrefix);
		}
	}

	// ─── Helpers for aligned output ──────────────────────────────────────

	uint32 CollectMaxTestNameLength(TestGroup& group)
	{
		uint32 maxLen = 0;
		for (const TestEntry& entry : group.tests)
			maxLen = std::max(maxLen, static_cast<uint32>(entry.func->GetName().length()));

		for (TestGroup& subGroup : group.subGroups)
			maxLen = std::max(maxLen, CollectMaxTestNameLength(subGroup));

		return maxLen;
	}

	// ─── Recursive match checker (non-recursive-lambda via function pointer) ──

	static bool GroupHasMatch(TestGroup& g, const Core::String& gPrefix, const Core::String& filter)
	{
		for (const TestEntry& entry : g.tests)
		{
			Core::String path;
			BuildTestPath(gPrefix, entry, path);
			if (WildcardMatch(path, filter)) return true;
		}
		for (TestGroup& sg : g.subGroups)
		{
			Core::String sgPrefix = gPrefix.empty() ? sg.name : gPrefix + "." + sg.name;
			if (GroupHasMatch(sg, sgPrefix, filter)) return true;
		}
		return false;
	}

	// ─── Filtered test runner ────────────────────────────────────────────

	void IncrementTotalTestCount()
	{
		++s_TotalTests;
	}

	uint32 RunFilteredTests(TestGroup& group, const Core::String& prefix,
	                        const Core::String& filter, uint32 indent, uint32 nameFieldWidth)
	{
	uint32 executedCount = 0;

	// If filter is empty, run all tests (no filtering).
	bool filterActive = !filter.empty();

	// When filter is active, check if this group or any subgroup has matching tests.
	if (filterActive && !prefix.empty())
	{
		bool anyMatch = GroupHasMatch(group, prefix, filter);
		if (!anyMatch)
			return 0;
	}

	const Core::String indentStr(indent * 2, ' ');
	std::cout << indentStr << "Group: " << group.name << std::endl;

	for (TestEntry& entry : group.tests)
	{
		Core::String path;
		BuildTestPath(prefix, entry, path);

		if (filterActive && !WildcardMatch(path, filter))
			continue;

		executedCount++;
		IncrementTotalTestCount();

		Core::String testLabel = indentStr + "  Test: " + entry.func->GetName();
		std::cout << std::left << std::setw(static_cast<int>(nameFieldWidth + static_cast<uint32>(indentStr.length()) + 8))
		          << testLabel;

		// Invoke the test method on its owning instance.
		// Use crash handler to catch SEH exceptions and C++ exceptions without disrupting the suite.
		CrashInfo crashInfo;
		auto result = InvokeWithCrashHandling([&entry]() {
			entry.func->Invoke(entry.ownerInstance, {});
		}, &crashInfo);

		switch (result)
		{
		case InvocationResult::Success:
			// Test completed normally — it should have called TEST_PASSED().
			break;
		case InvocationResult::TestFailed:
			// Failure already reported by the assertion macro; just skip TEST_PASSED().
			break;
		default:
			Testing::s_FailedTests++;
			Logging::Logger::SetConsoleColor(Logging::Logger::Error);

			switch (result)
			{
			case InvocationResult::SehCrash:
				if (crashInfo.description.empty())
					std::cout << "Crashed: SEH exception" << std::endl;
				else
					std::cout << "Crashed: SEH " << crashInfo.description << std::endl;
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

	for (TestGroup& subGroup : group.subGroups)
	{
		Core::String sgPrefix = prefix.empty() ? subGroup.name : prefix + "." + subGroup.name;
		executedCount += RunFilteredTests(subGroup, sgPrefix, filter, indent + 1, nameFieldWidth);
	}

	return executedCount;
}

} // namespace Testing
