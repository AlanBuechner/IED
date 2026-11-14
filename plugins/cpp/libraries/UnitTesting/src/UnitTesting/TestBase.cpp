#include "TestBase.h"
#include <charconv>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <exception>
#include <mutex>
#include <atomic>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <pthread.h>
#include <time.h>
#endif
#include "CrashHandler.h"

namespace Testing
{

	// --- File-local test result counters ------------------------------------

	uint32 s_TotalTests = 0;
	uint32 s_PassedTests = 0;
	uint32 s_FailedTests = 0;
	uint32 s_RunawayTests = 0; // still running past their timeout


	// --- TestGroup ----------------------------------------------------------

	Testing::TestGroup* TestGroup::GetOrCreateSubGroup(const Core::String& name)
	{
		for (uint32 i = 0; i < subGroups.Count(); i++) {
			if (subGroups[i].name == name)
				return &subGroups[i];
		}

		subGroups.Push({});
		subGroups.Back().name = name;
		return &subGroups.Back();
	}

	static const uint32 DEFAULT_TEST_TIMEOUT_MS = 5000; // 5 seconds per test, unless it says otherwise

	static uint32 TimeoutOf(const Reflect::Function& func)
	{
		if (!func.HasAttribute("TestTimeoutSeconds"))
			return DEFAULT_TEST_TIMEOUT_MS;
		const Core::String& seconds = func.GetAttribute("TestTimeoutSeconds").GetValue();
		uint32 value = 0;
		const auto [end, error] = std::from_chars(seconds.data(), seconds.data() + seconds.size(), value);
		return error == std::errc() && value > 0 ? value * 1000 : DEFAULT_TEST_TIMEOUT_MS;
	}

	void TestGroup::RegisterTests(const Reflect::Class& testClass, void* ownerInstance)
	{
		const std::vector<Reflect::Function>& funcs = testClass.GetFunctions();

		for (const Reflect::Function& func : funcs)
		{
			if (!func.HasAttribute("TestGroup"))
				continue;

			const Reflect::Attribute& testGroupAttrib = func.GetAttribute("TestGroup");
			std::stringstream ss(testGroupAttrib.GetValue());
			Core::String segment;
			Core::Vector<Core::String> groupNames;

			while (std::getline(ss, segment, '.'))
				groupNames.Push(segment);

			TestGroup* group = this;
			for (const Core::String& groupName : groupNames)
				group = group->GetOrCreateSubGroup(groupName);

			group->tests.Push({});
			group->tests.Back().func = &func;
			group->tests.Back().ownerInstance = ownerInstance;
			group->tests.Back().timeoutMs = TimeoutOf(func);
		}
	}

	// --- Helpers for aligned output ----------------------------------------

	static uint32 CollectMaxTestNameLength(TestGroup& group)
	{
		uint32 maxLen = 0;
		for (TestEntry& entry : group.tests)
			maxLen = std::max(maxLen, static_cast<uint32>(entry.func->GetName().length()));

		for (TestGroup& subGroup : group.subGroups)
			maxLen = std::max(maxLen, CollectMaxTestNameLength(subGroup));

		return maxLen;
	}

	// Shared data passed to each test thread.
	// Allocated on heap so it survives thread destruction.
	struct TestThreadData
	{
		const Reflect::Function* func;
		void* ownerInstance;
		std::atomic<bool> completed{false};
		InvocationResult result{InvocationResult::Success};
		CrashInfo crashInfo{};
		bool timedOut{false};
	};

	/// Runs a single test on the calling thread (its own: TestThreadProc).
	static void RunTestOnThisThread(TestThreadData* data)
	{
		CrashInfo crashInfo{};
		auto invocationResult = InvokeWithCrashHandling([data] {
			data->func->Invoke(data->ownerInstance, {});
		}, &crashInfo);

		data->result = invocationResult;
		if (invocationResult == InvocationResult::SehCrash)
			data->crashInfo = crashInfo;

		data->completed.store(true, std::memory_order_release);
	}

	static constexpr unsigned kTestThreadStackBytes = 8 * 1024 * 1024; // isolates deep recursion and stack overflows

	/// How a test's thread went: whether it started, whether it ran past its timeout, and the code a crash ended it with
	/// (Windows: an SEH exception's; 0 when it ended by itself, or on Linux, where a crash returns to the invocation).
	struct TestThreadOutcome
	{
		bool started = false;
		bool timedOut = false;
		uint32 exitCode = 0;
	};

#ifdef _WIN32
	/// Thread entry point for running a single test.
	static unsigned int __stdcall TestThreadProc(void* arg)
	{
		RunTestOnThisThread(static_cast<TestThreadData*>(arg));
		return 0;
	}

	/// Runs the test on a thread of its own and waits at most `timeoutMs` for it; a thread past that is left running.
	static TestThreadOutcome RunOnTestThread(TestThreadData* data, uint32 timeoutMs)
	{
		HANDLE threadHandle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, kTestThreadStackBytes, TestThreadProc, data, 0, nullptr));
		if (threadHandle == nullptr)
			return {};

		// Wait for the thread to complete with a timeout. If it dies from an unrecoverable exception
		// (like stack overflow) or hangs in an infinite loop, we detect it here.
		TestThreadOutcome outcome;
		outcome.started = true;
		outcome.timedOut = WaitForSingleObject(threadHandle, timeoutMs) == WAIT_TIMEOUT;

		// Get the exit code of the dead thread to detect SEH crashes.
		DWORD exitCode = 0;
		GetExitCodeThread(threadHandle, &exitCode);
		CloseHandle(threadHandle);
		outcome.exitCode = exitCode == STILL_ACTIVE ? 0 : exitCode;
		return outcome;
	}
#else
	/// Thread entry point for running a single test.
	static void* TestThreadProc(void* arg)
	{
		RunTestOnThisThread(static_cast<TestThreadData*>(arg));
		return nullptr;
	}

	/// Runs the test on a thread of its own and waits at most `timeoutMs` for it; a thread past that is left running
	/// (detached, so nothing waits for it).
	static TestThreadOutcome RunOnTestThread(TestThreadData* data, uint32 timeoutMs)
	{
		pthread_attr_t attributes;
		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, kTestThreadStackBytes);
		pthread_t thread;
		const int created = pthread_create(&thread, &attributes, TestThreadProc, data);
		pthread_attr_destroy(&attributes);
		if (created != 0)
			return {};

		timespec deadline{};
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += timeoutMs / 1000;
		deadline.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
		if (deadline.tv_nsec >= 1000000000L)
		{
			deadline.tv_sec += 1;
			deadline.tv_nsec -= 1000000000L;
		}

		TestThreadOutcome outcome;
		outcome.started = true;
		outcome.timedOut = pthread_timedjoin_np(thread, nullptr, &deadline) == ETIMEDOUT;
		if (outcome.timedOut)
			pthread_detach(thread);
		return outcome;
	}
#endif

	/// Run a single test with full crash isolation.
	/// Each test runs in its own thread with 8MB stack so:
	/// - crashes (SEH access violations, etc.; on Linux fault signals) end only the child thread's test; the parent sees
	///   the code in the thread's exit code (Windows) or the test's result (Linux)
	/// - Stack overflows likewise
	/// - Hung tests are left running past their timeout and reported
	/// Returns true if the test passed, false otherwise.

	bool RunSingleTest(const TestEntry& entry, uint32 indent, uint32 nameFieldWidth)
	{
		const Core::String indentStr(indent * 2, ' ');
		Core::String testLabel = indentStr + "  Test: " + entry.func->GetName();

		// Print and flush the test name BEFORE running so we always see which test crashed.
		std::cout << std::left << std::setw(static_cast<int>(nameFieldWidth + static_cast<uint32>(indentStr.length()) + 8))
		          << testLabel << std::flush;

		// Allocate thread data on the heap so it survives thread destruction.
		TestThreadData* threadData = new TestThreadData();
		threadData->func = entry.func;
		threadData->ownerInstance = entry.ownerInstance;

		const TestThreadOutcome outcome = RunOnTestThread(threadData, entry.timeoutMs);
		if (!outcome.started)
		{
			delete threadData;
			s_TotalTests++;
			s_FailedTests++;
			Logging::Logger::SetConsoleColor(Logging::Logger::Error);
			std::cout << "Failed: could not create test thread" << std::endl;
			Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
			return false;
		}

		const bool timedOut = outcome.timedOut;
		// A test past its timeout is left running, never terminated: a thread killed while it holds the heap's lock (or any
		// other) deadlocks every later allocation. It keeps its data and instance; the run ends the process at the end
		// (EndRunWithoutRunaways) instead of waiting for it.
		if (timedOut) s_RunawayTests++;
		const uint32 exitCode = outcome.exitCode;

		bool passed = false;

		if (!timedOut && threadData->completed.load(std::memory_order_acquire))
		{
			// Thread completed normally (C++ exception caught or test succeeded).
			switch (threadData->result)
			{
			case InvocationResult::Success:
				s_TotalTests++;
				s_PassedTests++;
				Logging::Logger::SetConsoleColor(Logging::Logger::Info);
				std::cout << "Passed" << std::endl;
				Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
				passed = true;
				break;
			case InvocationResult::TestFailed:
				s_TotalTests++;
				passed = false;
				break;
			default:
				s_TotalTests++;
				s_FailedTests++;

				Logging::Logger::SetConsoleColor(Logging::Logger::Error);

				switch (threadData->result)
				{
				case InvocationResult::SehCrash:
					std::cout << "Crashed: " << CrashSummary(threadData->crashInfo) << std::endl;
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
		else
		{
			// Thread died without completing - SEH crash, stack overflow, or timeout.
			s_TotalTests++;
			s_FailedTests++;

			Logging::Logger::SetConsoleColor(Logging::Logger::Error);
			if (timedOut)
				std::cout << "Crashed: test timed out after " << entry.timeoutMs / 1000 << "s" << std::endl;
			else if (exitCode != 0)
			{
				// Thread died from an SEH exception. Format the code.
				std::ostringstream oss;
				oss << "Crashed: SEH 0x" << std::hex << std::setfill('0') << std::setw(8) << exitCode;

				switch (exitCode)
				{
				case 0xC0000005u: oss << " (Access Violation)"; break;
				case 0x80000003u: oss << " (Breakpoint/__debugbreak)"; break;
				case 0xC00000FDu: oss << " (Stack Overflow)"; break;
				case 0xC0000094u: oss << " (Integer Divide By Zero)"; break;
				case 0xC000001Du: oss << " (Illegal Instruction)"; break;
				default: break;
				}

				std::cout << oss.str() << std::endl;
			}
			else
				std::cout << "Crashed: thread terminated abnormally" << std::endl;
			Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
		}

		if (!timedOut) delete threadData;
		return passed;
	}

	static void RunTestGroup(TestGroup& group, uint32 indent, uint32 nameFieldWidth)
	{
		const Core::String indentStr(indent * 2, ' ');
		std::cout << indentStr << "Group: " << group.name << std::endl;

		for (TestEntry& entry : group.tests)
		{
			RunSingleTest(entry, indent, nameFieldWidth);
		}

		for (TestGroup& subGroup : group.subGroups)
			RunTestGroup(subGroup, indent + 1, nameFieldWidth);
	}

	void Test::ReportFailure(const Core::String& msg)
	{
		TestFailedI(msg);
	}

	void Test::TestFailedI(const Core::String& msg)
	{
		s_FailedTests++;
		Logging::Logger::SetConsoleColor(Logging::Logger::Error);
		std::cout << "Failed: " << msg << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
	}

	// --- Global entry point ------------------------------------------------

	void RunTests()
	{
		const std::vector<const Reflect::Class*>& testClasses =
			Reflect::Registry::GetRegistry()->GetGroup("Testing");

		// Single root group that accumulates every test class.
		TestGroup rootGroup;
		rootGroup.name = "All Tests";

		// Phase 1: Collect all tests from every reflected test class.
		// Keep each instance alive so test methods have a valid 'this' pointer.
		std::vector<Test*> instances;

		for (const Reflect::Class* testClass : testClasses)
		{
			Test* instance = static_cast<Test*>(testClass->CreateInstance());
			rootGroup.RegisterTests(*testClass, instance);
			instances.push_back(instance);
		}

		// Phase 2: Run all collected tests.
		s_TotalTests = 0;
		s_PassedTests = 0;
		s_FailedTests = 0;

		uint32 maxNameLen = CollectMaxTestNameLength(rootGroup);
		RunTestGroup(rootGroup, 0, maxNameLen);

		// Phase 3: Print summary.
		std::cout << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Info);
		std::cout << "========================================" << std::endl;
		std::cout << "Test Summary" << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
		std::cout << "  Total:  " << s_TotalTests << std::endl;
		Logging::Logger::SetConsoleColor(Logging::Logger::Info);
		std::cout << "  Passed: " << s_PassedTests << std::endl;

		if (s_FailedTests > 0) {
			Logging::Logger::SetConsoleColor(Logging::Logger::Error);
			std::cout << "  Failed: " << s_FailedTests << std::endl;
		} else {
			std::cout << "  Failed: " << s_FailedTests << std::endl;
		}

		Logging::Logger::SetConsoleColor(Logging::Logger::Trace);
		std::cout << "========================================" << std::endl;

		// Clean up instances, unless a runaway test may still use its own.
		if (s_RunawayTests == 0)
			for (Test* instance : instances)
				delete instance;
	}

	uint32 GetRunawayTestCount()
	{
		return s_RunawayTests;
	}

	uint32 GetFailedTestCount()
	{
		return s_FailedTests;
	}

	uint32 GetTotalTestCount()
	{
		return s_TotalTests;
	}

	uint32 GetPassedTestCount()
	{
		return s_PassedTests;
	}

}
