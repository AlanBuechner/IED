#pragma once
#include "Core/Core.h"
#include <cstdint>
#include <functional>

namespace Testing
{
	/// Result of a test/benchmark invocation that may have crashed.
	enum class InvocationResult
	{
		Success,        /// Normal completion (may still call TEST_PASSED inside).
		TestFailed,     /// Caught TestFailedException or BenchmarkFailedException.
		StdException,   /// Caught a std::exception-derived exception.
		UnknownException, /// Caught a non-std C++ exception.
		SehCrash,       /// A crash: a Windows SEH exception (access violation, etc.), or on Linux a fault signal (SIGSEGV, etc.).
	};

	/// Information about a crash, filled in when InvocationResult is SehCrash.
	struct CrashInfo
	{
		uint32_t code;              /// Windows exception code (e.g., 0xC0000005), or on Linux the signal's number.
		Core::String description;   /// Human-readable description of the crash.
	};

	/// What a run prints after "Crashed: " for a crash: "SEH <description>" on Windows, the signal on Linux.
	Core::String CrashSummary(const CrashInfo& crash);

	/// Internal non-template implementation. Accepts std::function so it doesn't
	/// need to see TestFailedException/BenchmarkFailedException at header level.
	InvocationResult InvokeWithCrashHandlingImpl(std::function<void()> invokeFn, CrashInfo* outCrashInfo);

	/// Invoke a test or benchmark function with full crash protection (SEH or fault signals + C++ exceptions).
	/// Accepts any callable (function pointer, lambda with captures, std::function).
	template<typename F>
	InvocationResult InvokeWithCrashHandling(F&& invokeFn, CrashInfo* outCrashInfo = nullptr)
	{
		return InvokeWithCrashHandlingImpl(std::function<void()>(std::forward<F>(invokeFn)), outCrashInfo);
	}

	/// Install the crash interception (on Windows a no-op: each invocation installs its own handler; on Linux the
	/// fault signals' handlers, which InvokeWithCrashHandling also installs on first use). Call once at startup.
	void InstallCrashHandler();

	/// Remove what InstallCrashHandler() installed.
	void UninstallCrashHandler();
}
