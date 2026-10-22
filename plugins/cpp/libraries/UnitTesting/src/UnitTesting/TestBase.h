#pragma once
#include "Reflection.h"
#include "Core/Core.h"
#include "Core/String.h"
#include "Core/Vector.h"

#define TEST_FUNC(group) FUNCTION(TestGroup=group)
// A test that needs longer than the default 5 seconds (it builds and runs a program, say).
#define TEST_FUNC_TIMEOUT(group, seconds) FUNCTION(TestGroup=group, TestTimeoutSeconds=seconds)
// Early-exit signal only. The RUNNER records a completed, non-failing test as a pass exactly once;
// this macro just stops the test body early (e.g. after a conditional branch). A test that returns
// normally without ever failing an assertion is therefore still counted as passed, so forgetting the
// trailing call can no longer silently drop a test from the Passed count.
#define TEST_PASSED() return;
#define TEST_FAILED(...) { Test::TestFailed(__VA_ARGS__); return; }

/// Exception thrown when a test assertion fails. Caught by the test runner to stop
/// the current test immediately and prevent running code on invalid state.
class TestFailedException : public std::exception
{
public:
	explicit TestFailedException(const Core::String& msg) : m_Msg(msg) {}
	const char* what() const noexcept override { return m_Msg.c_str(); }
private:
	Core::String m_Msg;
};

// Benchmark macros and types - defined in BenchmarkTest.h (BenchmarkFailedException is in namespace Testing).
#include "BenchmarkTest.h"

namespace Testing
{
	// Shared result counters - defined in TestBase.cpp, used by filter runners.
	extern uint32 s_FailedTests;
	extern uint32 s_PassedTests;
	extern uint32 s_TotalTests;
	/// A single registered test - pairs the reflected function with its owning instance.
	struct TestEntry
	{
		const Reflect::Function* func;
		void* ownerInstance;
		uint32 timeoutMs;
	};

	struct TestGroup
	{
		Core::String name;
		Core::Vector<TestGroup> subGroups;
		Core::Vector<TestEntry> tests;

		TestGroup* GetOrCreateSubGroup(const Core::String& name);

		/// Registers the test functions of one reflected class into this group.
		/// Each function tagged with TEST_FUNC becomes a leaf under its hierarchical group path.
		void RegisterTests(const Reflect::Class& testClass, void* ownerInstance);
	};

	/// Collects all registered test functions from every reflected test class into
	/// a single hierarchical TestGroup structure, then runs them together.
	void RunTests();

	/// Returns the number of failed tests from the last run (full or filtered).
	uint32 GetFailedTestCount();

	/// Tests that timed out and are still running (they count as failed).
	uint32 GetRunawayTestCount();

	/// Returns the total number of tests executed in the last run (full or filtered).
	uint32 GetTotalTestCount();

	/// Returns the number of passed tests from the last run (full or filtered).
	uint32 GetPassedTestCount();

	class Test : public Reflect::Reflected
	{
	public:
		virtual ~Test() = default;

	/// Report an assertion failure from a static context (used by TestAssertions.h).
	static void ReportFailure(const Core::String& msg);

	protected:

		template<typename ... Args>
		void TestFailed(const Core::String& msg = "", const Args&... args) {
			TestFailedI(Core::StringFormat::Format(msg.c_str(), args...));
		}

	private:
		static void TestFailedI(const Core::String& msg);
	};
}
