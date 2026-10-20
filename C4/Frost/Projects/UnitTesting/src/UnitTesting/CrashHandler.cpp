// NOMINMAX must be defined before any Windows headers to prevent min/max macros
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CrashHandler.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

// Define SEH codes not always available in older SDKs
#ifndef EXCEPTION_INTEGER_DIVIDE_BY_ZERO
#define EXCEPTION_INTEGER_DIVIDE_BY_ZERO 0xC000008E
#endif
#ifndef EXCEPTION_INTEGER_OVERFLOW
#define EXCEPTION_INTEGER_OVERFLOW 0xC0000095
#endif

// Include the test/benchmark base headers for their exception types.
#include "TestBase.h"       // TestFailedException (global namespace)
#include "BenchmarkTest.h"  // BenchmarkFailedException (Testing namespace)

namespace Testing
{
	// ─── Per-Thread SEH Crash Detection Strategy ──────────────
	//
	// Each test runs in its own thread. Before invoking the test, the thread
	// installs a first-position vectored exception handler. When an SEH exception
	// occurs:
	//   1. The vectored handler records the exception code in thread-local storage.
	//   2. The handler removes itself from the chain to avoid re-entry issues.
	//   3. The handler returns EXCEPTION_EXECUTE_HANDLER and calls ExitThread().
	//      This marks the exception as "handled" (so TerminateProcess is NOT called)
	//      and terminates only the child thread.
	//   4. The parent thread detects the dead thread via WaitForSingleObject + GetExitCodeThread.
	//
	// We deliberately do NOT use setjmp/longjmp because longjmp skips C++ destructors,
	// which corrupts heap state and eventually kills the entire process with STATUS_HEAP_CORRUPTION.
	// ExitThread is an OS call that properly cleans up thread resources at the kernel level.
	// Local stack-frame destructors are skipped, but test instances live in the parent thread.

	// Thread-local storage to record the SEH exception that killed this thread.
	thread_local DWORD s_ThreadExceptionCode = 0;

	// Per-thread vectored handler token (installed/uninstalled per test invocation).
	thread_local PVOID s_ThreadHandlerToken = nullptr;

	static LONG WINAPI ThreadVectoredExceptionHandler(PEXCEPTION_POINTERS exceptionPointers)
	{
		DWORD code = exceptionPointers->ExceptionRecord->ExceptionCode;

		// Ignore debugger single-step exceptions — let them pass through.
		if (code == EXCEPTION_SINGLE_STEP ||
		    code == 0x40000015) // RPC Exception
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// CRITICAL: Let C++ exceptions pass through to the C++ runtime.
		// MSVCRT uses SEH internally for C++ exception dispatch. The code 0xe06d7363
		// is a C++ exception, NOT a crash. If we catch it here, the try/catch in
		// InvokeWithCrashHandlingImpl never sees the TestFailedException and the
		// thread dies with a false "Crashed" report alongside the real "Failed" message.
		if (code == 0xe06d7363 ||      // MSVCRT C++ exception
		    code == 0x1dcb3489 ||      // C++ EH exception
		    code == 0xe0434352)        // CLR exception
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// Record the exception code so the parent can report it.
		s_ThreadExceptionCode = code;

		// Remove ourselves from the handler chain to avoid re-entry issues if ExitThread
		// triggers another exception (unlikely, but safe).
		if (s_ThreadHandlerToken)
		{
			RemoveVectoredExceptionHandler(s_ThreadHandlerToken);
			s_ThreadHandlerToken = nullptr;
		}

		// Mark the exception as handled (EXCEPTION_EXECUTE_HANDLER) so Windows does NOT
		// call TerminateProcess. Then terminate only this thread with the exception code.
		// The parent will detect the dead thread via WaitForSingleObject + GetExitCodeThread.
		// ExitThread is a kernel call that doesn't need much stack — safe even near overflow.
		ExitThread(code);

		// Should never reach here, but just in case:
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void InstallCrashHandler()
	{
		// Global install is a no-op now — handlers are installed per-thread in InvokeWithCrashHandlingImpl.
		// Kept for API compatibility.
	}

	void UninstallCrashHandler()
	{
		// No-op — handlers are removed per-thread after each invocation.
	}

	// ─── Non-template implementation with SEH + C++ exception handling ──

	InvocationResult InvokeWithCrashHandlingImpl(std::function<void()> invokeFn, CrashInfo* outCrashInfo)
	{
		s_ThreadExceptionCode = 0;

		// Install a first-position vectored handler for THIS thread only.
		// This catches SEH exceptions before the OS default filter can terminate the process.
		s_ThreadHandlerToken = AddVectoredExceptionHandler(1, ThreadVectoredExceptionHandler);

		// Run the actual function with C++ exception handling.
		// If an SEH exception occurs, the vectored handler records the code and lets
		// the thread die. The parent detects this via WaitForSingleObject + GetExitCodeThread.
		try
		{
			invokeFn();
		}
		catch (const TestFailedException&)
		{
			if (s_ThreadHandlerToken)
			{
				RemoveVectoredExceptionHandler(s_ThreadHandlerToken);
				s_ThreadHandlerToken = nullptr;
			}
			return InvocationResult::TestFailed;
		}
		catch (const Testing::BenchmarkFailedException&)
		{
			if (s_ThreadHandlerToken)
			{
				RemoveVectoredExceptionHandler(s_ThreadHandlerToken);
				s_ThreadHandlerToken = nullptr;
			}
			return InvocationResult::TestFailed;
		}
		catch (const std::exception&)
		{
			if (s_ThreadHandlerToken)
			{
				RemoveVectoredExceptionHandler(s_ThreadHandlerToken);
				s_ThreadHandlerToken = nullptr;
			}
			return InvocationResult::StdException;
		}
		catch (...)
		{
			if (s_ThreadHandlerToken)
			{
				RemoveVectoredExceptionHandler(s_ThreadHandlerToken);
				s_ThreadHandlerToken = nullptr;
			}
			return InvocationResult::UnknownException;
		}

		// Normal completion — clean up handler.
		if (s_ThreadHandlerToken)
		{
			RemoveVectoredExceptionHandler(s_ThreadHandlerToken);
			s_ThreadHandlerToken = nullptr;
		}
		return InvocationResult::Success;
	}

	// ─── SEH Exception Code Formatting ──────────────────────────────────

	Core::String FormatSehException(uint32_t code)
	{
		std::ostringstream oss;
		oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << code;

		const char* name = nullptr;
		switch (code)
		{
		case EXCEPTION_ACCESS_VIOLATION:        name = "Access Violation"; break;
		case EXCEPTION_BREAKPOINT:              name = "Breakpoint (__debugbreak)"; break;
		case EXCEPTION_STACK_OVERFLOW:          name = "Stack Overflow"; break;
		case EXCEPTION_INTEGER_DIVIDE_BY_ZERO:  name = "Integer Divide By Zero"; break;
		case EXCEPTION_INTEGER_OVERFLOW:        name = "Integer Overflow"; break;
		case EXCEPTION_DATATYPE_MISALIGNMENT:   name = "Data Type Misalignment"; break;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   name = "Array Bounds Exceeded"; break;
		case EXCEPTION_IN_PAGE_ERROR:           name = "In-Page Error"; break;
		case EXCEPTION_INVALID_HANDLE:          name = "Invalid Handle"; break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:     name = "Illegal Instruction"; break;
		default:                                name = nullptr; break;
		}

		if (name)
			oss << " (" << name << ")";

		return Core::String(oss.str());
	}

} // namespace Testing
