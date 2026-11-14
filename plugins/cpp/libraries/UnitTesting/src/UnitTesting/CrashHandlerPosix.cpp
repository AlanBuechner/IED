// The Linux crash interception (fault signals); Windows' is CrashHandler.cpp.
#ifndef _WIN32
#include "CrashHandler.h"
#include "TestBase.h"       // TestFailedException (global namespace)
#include "BenchmarkTest.h"  // BenchmarkFailedException (Testing namespace)
#include <csetjmp>
#include <memory>
#include <mutex>
#include <signal.h>
#include <string>

namespace Testing
{
	// --- Per-Thread Fault Recovery -----------------------------------------
	//
	// A fault in a test (SIGSEGV, SIGBUS, SIGFPE, SIGILL, or the SIGTRAP of an assertion's debug break) jumps back to
	// the invocation that runs it, on the faulting thread, which reports the signal. The frames in between are abandoned
	// (their destructors do not run), as Windows' ExitThread abandons them. The handler runs on an alternate stack of the
	// thread's own, so a stack overflow is caught too. A fault outside any invocation ends the process as it would have.
	// SIGABRT is left alone: abort() ends the process on Windows too.

	namespace
	{
		constexpr int kFaultSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP };
		constexpr size_t kFaultSignalCount = sizeof(kFaultSignals) / sizeof(kFaultSignals[0]);
		constexpr size_t kAlternateStackBytes = 64 * 1024;

		// The invocation a fault on this thread returns to; none outside one.
		thread_local sigjmp_buf* t_Recovery = nullptr;

		// The thread's alternate signal stack, given up before its memory is.
		struct AlternateStack
		{
			std::unique_ptr<char[]> memory;

			~AlternateStack()
			{
				if (!memory) return;
				stack_t off{};
				off.ss_flags = SS_DISABLE;
				sigaltstack(&off, nullptr);
			}
		};
		thread_local AlternateStack t_AlternateStack;

		std::mutex s_InstallLock;
		bool s_Installed = false;
		struct sigaction s_Previous[kFaultSignalCount];

		void OnFault(int signal, siginfo_t*, void*)
		{
			if (sigjmp_buf* recovery = t_Recovery)
			{
				t_Recovery = nullptr;
				siglongjmp(*recovery, signal);
			}
			struct sigaction defaults{};
			defaults.sa_handler = SIG_DFL;
			sigaction(signal, &defaults, nullptr);
			raise(signal);
		}

		void EnsureAlternateStack()
		{
			if (t_AlternateStack.memory) return;
			t_AlternateStack.memory = std::make_unique<char[]>(kAlternateStackBytes);
			stack_t stack{};
			stack.ss_sp = t_AlternateStack.memory.get();
			stack.ss_size = kAlternateStackBytes;
			sigaltstack(&stack, nullptr);
		}

		const char* SignalName(int signal)
		{
			switch (signal)
			{
			case SIGSEGV: return "Segmentation Fault, SIGSEGV";
			case SIGBUS:  return "Bus Error, SIGBUS";
			case SIGFPE:  return "Arithmetic Exception, SIGFPE";
			case SIGILL:  return "Illegal Instruction, SIGILL";
			case SIGTRAP: return "Breakpoint (debug break), SIGTRAP";
			default:      return "SIG?";
			}
		}

		InvocationResult InvokeCatching(const std::function<void()>& invokeFn)
		{
			try
			{
				invokeFn();
			}
			catch (const TestFailedException&)
			{
				return InvocationResult::TestFailed;
			}
			catch (const Testing::BenchmarkFailedException&)
			{
				return InvocationResult::TestFailed;
			}
			catch (const std::exception&)
			{
				return InvocationResult::StdException;
			}
			catch (...)
			{
				return InvocationResult::UnknownException;
			}
			return InvocationResult::Success;
		}
	}

	void InstallCrashHandler()
	{
		std::lock_guard<std::mutex> lock(s_InstallLock);
		if (s_Installed) return;
		struct sigaction action{};
		action.sa_sigaction = OnFault;
		action.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigemptyset(&action.sa_mask);
		for (size_t i = 0; i < kFaultSignalCount; ++i)
			sigaction(kFaultSignals[i], &action, &s_Previous[i]);
		s_Installed = true;
	}

	void UninstallCrashHandler()
	{
		std::lock_guard<std::mutex> lock(s_InstallLock);
		if (!s_Installed) return;
		for (size_t i = 0; i < kFaultSignalCount; ++i)
			sigaction(kFaultSignals[i], &s_Previous[i], nullptr);
		s_Installed = false;
	}

	InvocationResult InvokeWithCrashHandlingImpl(std::function<void()> invokeFn, CrashInfo* outCrashInfo)
	{
		InstallCrashHandler();
		EnsureAlternateStack();
		sigjmp_buf recovery;
		sigjmp_buf* const outer = t_Recovery; // an invocation inside another returns to it afterwards
		if (const int signal = sigsetjmp(recovery, 1); signal != 0)
		{
			t_Recovery = outer;
			if (outCrashInfo)
				*outCrashInfo = CrashInfo{ static_cast<uint32_t>(signal), "signal " + std::to_string(signal) + " (" + SignalName(signal) + ")" };
			return InvocationResult::SehCrash;
		}
		t_Recovery = &recovery;
		const InvocationResult result = InvokeCatching(invokeFn);
		t_Recovery = outer;
		return result;
	}

	Core::String CrashSummary(const CrashInfo& crash)
	{
		return crash.description.empty() ? Core::String("signal") : crash.description;
	}
} // namespace Testing
#endif
