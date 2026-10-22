#pragma once
#include "Core/Core.h"
#include <cstdint>
#include <functional>

namespace Testing
{
	/// Result of a test/benchmark invocation that may have crashed via SEH.
	enum class InvocationResult
	{
		Success,        /// Normal completion (may still call TEST_PASSED inside).
		TestFailed,     /// Caught TestFailedException or BenchmarkFailedException.
		StdException,   /// Caught a std::exception-derived exception.
		UnknownException, /// Caught a non-std C++ exception.
		SehCrash,       /// Caught a Windows SEH exception (access violation, etc.).
	};

	/// Information about an SEH crash, filled in when InvocationResult is SehCrash.
	struct CrashInfo
	{
		uint32_t code;              /// Windows exception code (e.g., 0xC0000005).
		Core::String description;   /// Human-readable description of the crash.
	};

	/// Internal non-template implementation. Accepts std::function so it doesn't
	/// need to see TestFailedException/BenchmarkFailedException at header level.
	InvocationResult InvokeWithCrashHandlingImpl(std::function<void()> invokeFn, CrashInfo* outCrashInfo);

	/// Invoke a test or benchmark function with full crash protection (SEH + C++ exceptions).
	/// Accepts any callable (function pointer, lambda with captures, std::function).
	template<typename F>
	InvocationResult InvokeWithCrashHandling(F&& invokeFn, CrashInfo* outCrashInfo = nullptr)
	{
		return InvokeWithCrashHandlingImpl(std::function<void()>(std::forward<F>(invokeFn)), outCrashInfo);
	}

	/// Install the vectored exception handler for crash interception. Call once at startup.
	void InstallCrashHandler();

	/// Remove the vectored exception handler installed by InstallCrashHandler().
	void UninstallCrashHandler();
}
