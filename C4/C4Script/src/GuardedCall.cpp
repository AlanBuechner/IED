// Guarded calls into script code (docs/runtime/04 section 7): a hardware fault stops the call and is reported, so a
// crashing script does not take the program down. It lives here, not in the C4 runtime: the runtime is compiled with
// -mstackrealign, under which clang miscompiles SEH __try / __except.

#include "C4Script.h"

#if defined(_WIN32)

#include <malloc.h>
#include <windows.h>

namespace
{
	bool IsFault(DWORD code)
	{
		switch (code)
		{
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_IN_PAGE_ERROR:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_INT_OVERFLOW:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_DATATYPE_MISALIGNMENT:
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		case EXCEPTION_FLT_INVALID_OPERATION:
		case EXCEPTION_FLT_OVERFLOW:
		case EXCEPTION_FLT_UNDERFLOW:
		case EXCEPTION_FLT_STACK_CHECK:
		case EXCEPTION_FLT_DENORMAL_OPERAND:
		case EXCEPTION_FLT_INEXACT_RESULT:
		case EXCEPTION_BREAKPOINT: // a trap (int3) no debugger took: what the optimizer leaves of undefined behavior
			return true;
		default:
			return false;
		}
	}

	// The __except filter: a fault is handled and described; anything else (a C++ or C4 exception) is not ours.
	int Describe(const EXCEPTION_POINTERS* info, c4s_fault* fault)
	{
		const EXCEPTION_RECORD* record = info->ExceptionRecord;
		if (!IsFault(record->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
		fault->code = record->ExceptionCode;
		fault->address = reinterpret_cast<uint64_t>(record->ExceptionAddress);
		const bool touchedMemory = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR;
		if (touchedMemory && record->NumberParameters >= 2)
		{
			fault->write = static_cast<uint32_t>(record->ExceptionInformation[0]);
			fault->target = static_cast<uint64_t>(record->ExceptionInformation[1]);
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

int32_t c4s_guarded_call(void (*fn)(void*), void* context, c4s_fault* fault)
{
	c4s_fault seen = {};
	__try
	{
		fn(context);
	}
	__except (Describe(GetExceptionInformation(), &seen))
	{
		if (seen.code == EXCEPTION_STACK_OVERFLOW) _resetstkoflw(); // the guard page, for the next overflow
		if (fault) *fault = seen;
		return 0;
	}
	return 1;
}

#else

// Linux: the faults arrive as signals. A handler for each, on the thread's own signal stack (so a stack overflow is
// caught too), jumps back to the guarded call running on the faulting thread; on a thread with none running the signal
// goes on to the handler it had before (the runtime's crash report, or the system's). A fault is described with the
// Windows exception code it would have had, which is what c4s_fault and its readers speak.

#include <csetjmp>
#include <csignal>
#include <cstring>
#include <mutex>
#include <sys/mman.h>
#include <time.h>
#include <ucontext.h>

namespace
{
	constexpr uint32_t kAccessViolation = 0xC0000005u;
	constexpr uint32_t kInPageError = 0xC0000006u;
	constexpr uint32_t kIllegalInstruction = 0xC000001Du;
	constexpr uint32_t kPrivilegedInstruction = 0xC0000096u;
	constexpr uint32_t kIntDivideByZero = 0xC0000094u;
	constexpr uint32_t kIntOverflow = 0xC0000095u;
	constexpr uint32_t kStackOverflow = 0xC00000FDu;
	constexpr uint32_t kMisaligned = 0x80000002u;
	constexpr uint32_t kFloatDivideByZero = 0xC000008Eu;
	constexpr uint32_t kFloatInexact = 0xC000008Fu;
	constexpr uint32_t kFloatInvalid = 0xC0000090u;
	constexpr uint32_t kFloatOverflow = 0xC0000091u;
	constexpr uint32_t kFloatUnderflow = 0xC0000093u;
	constexpr uint32_t kBreakpoint = 0x80000003u;

	constexpr int kSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
	constexpr int kSignalCount = sizeof(kSignals) / sizeof(kSignals[0]);
	constexpr uint64_t kOverflowReach = 65536; // a fault this near the stack pointer is the stack running out
	constexpr uint64_t kPageFaultTrap = 14, kPageFaultWrite = 0x2, kPageFaultFetch = 0x10;
	constexpr size_t kSignalStackBytes = 64 * 1024;
	constexpr int64_t kHandlerCheckNs = 100'000'000; // how often a thread looks whether someone replaced the handlers

	struct Guard
	{
		sigjmp_buf jump;
		c4s_fault fault;
	};

	thread_local Guard* t_Guard = nullptr;
	thread_local int64_t t_HandlersCheckedAt = 0; // 0: never

	std::mutex s_Install;
	struct sigaction s_Previous[kSignalCount];

	int IndexOf(int signal)
	{
		for (int i = 0; i < kSignalCount; ++i)
			if (kSignals[i] == signal) return i;
		return 0;
	}

	c4s_fault Describe(int signal, const siginfo_t* info, const ucontext_t* context)
	{
		c4s_fault fault = {};
		const auto& registers = context->uc_mcontext.gregs;
		fault.address = static_cast<uint64_t>(registers[REG_RIP]);
		const uint64_t target = reinterpret_cast<uint64_t>(info->si_addr);
		switch (signal)
		{
		case SIGSEGV:
		{
			const uint64_t stack = static_cast<uint64_t>(registers[REG_RSP]);
			if (target + kOverflowReach >= stack && target < stack + kOverflowReach)
			{
				fault.code = kStackOverflow;
				break;
			}
			fault.code = kAccessViolation;
			fault.target = target;
			const uint64_t error = static_cast<uint64_t>(registers[REG_ERR]);
			if (static_cast<uint64_t>(registers[REG_TRAPNO]) == kPageFaultTrap)
				fault.write = (error & kPageFaultFetch) ? 8 : (error & kPageFaultWrite) ? 1 : 0;
			break;
		}
		case SIGBUS:
			fault.code = info->si_code == BUS_ADRALN ? kMisaligned : kInPageError;
			fault.target = target;
			break;
		case SIGILL:
			fault.code = info->si_code == ILL_PRVOPC || info->si_code == ILL_PRVREG ? kPrivilegedInstruction : kIllegalInstruction;
			break;
		case SIGFPE:
			switch (info->si_code)
			{
			case FPE_INTDIV: fault.code = kIntDivideByZero; break;
			case FPE_INTOVF: fault.code = kIntOverflow; break;
			case FPE_FLTDIV: fault.code = kFloatDivideByZero; break;
			case FPE_FLTOVF: fault.code = kFloatOverflow; break;
			case FPE_FLTUND: fault.code = kFloatUnderflow; break;
			case FPE_FLTRES: fault.code = kFloatInexact; break;
			default: fault.code = kFloatInvalid; break;
			}
			break;
		default:
			// SIGTRAP: an int3 no debugger took, what the optimizer leaves of undefined behavior. The kernel reports the
			// instruction after it, Windows the int3.
			fault.code = kBreakpoint;
			if (info->si_code == SI_KERNEL && fault.address != 0) fault.address -= 1;
			break;
		}
		return fault;
	}

	// Not ours: on to the handler the signal had before. An instruction's fault comes again when the handler returns to
	// it, so the system's default ends the process with the signal; a signal that was sent (or a trap, which does not
	// come again) is raised again.
	void HandOn(int signal, siginfo_t* info, void* context)
	{
		const struct sigaction& previous = s_Previous[IndexOf(signal)];
		if (previous.sa_flags & SA_SIGINFO)
		{
			if (previous.sa_sigaction) previous.sa_sigaction(signal, info, context);
			return;
		}
		if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr)
		{
			previous.sa_handler(signal);
			return;
		}
		struct sigaction system;
		std::memset(&system, 0, sizeof(system));
		system.sa_handler = SIG_DFL;
		sigemptyset(&system.sa_mask);
		sigaction(signal, &system, nullptr);
		if (info->si_code <= 0 || signal == SIGTRAP) raise(signal);
	}

	void OnFault(int signal, siginfo_t* info, void* context)
	{
		Guard* guard = t_Guard;
		if (!guard)
		{
			HandOn(signal, info, context);
			return;
		}
		t_Guard = nullptr; // a fault while it leaves is not this call's
		guard->fault = Describe(signal, info, static_cast<const ucontext_t*>(context));
		siglongjmp(guard->jump, 1);
	}

	bool Ours(const struct sigaction& action) { return (action.sa_flags & SA_SIGINFO) && action.sa_sigaction == OnFault; }

	int64_t CoarseNow()
	{
		timespec now;
		clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
		return static_cast<int64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
	}

	// The handlers go in at the first guarded call, in front of what the program installed before (the runtime's crash
	// report, which they hand on to). A program that installs its own later puts ours behind it: each thread looks now and
	// then, at a guarded call, and puts them back in front.
	void InstallHandlers()
	{
		const int64_t now = CoarseNow();
		if (t_HandlersCheckedAt != 0 && now - t_HandlersCheckedAt < kHandlerCheckNs) return;
		t_HandlersCheckedAt = now;
		std::lock_guard<std::mutex> held(s_Install);
		for (int i = 0; i < kSignalCount; ++i)
		{
			struct sigaction current;
			if (sigaction(kSignals[i], nullptr, &current) != 0 || Ours(current)) continue;
			struct sigaction action;
			std::memset(&action, 0, sizeof(action));
			action.sa_sigaction = OnFault;
			action.sa_flags = SA_SIGINFO | SA_ONSTACK;
			sigemptyset(&action.sa_mask);
			if (sigaction(kSignals[i], &action, &s_Previous[i]) != 0) s_Previous[i] = current;
		}
	}

	// The thread's stack for the handler, so it runs when the fault is the stack running out. A thread that has one (the
	// runtime gives each of its threads one, c4_crash_attach_thread) keeps it; this one is freed as the thread ends.
	struct SignalStack
	{
		void* memory = nullptr;

		void Attach()
		{
			stack_t current;
			if (memory || sigaltstack(nullptr, &current) != 0 || !(current.ss_flags & SS_DISABLE)) return;
			void* mapped = mmap(nullptr, kSignalStackBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
			if (mapped == MAP_FAILED) return;
			stack_t stack;
			std::memset(&stack, 0, sizeof(stack));
			stack.ss_sp = mapped;
			stack.ss_size = kSignalStackBytes;
			if (sigaltstack(&stack, nullptr) != 0)
			{
				munmap(mapped, kSignalStackBytes);
				return;
			}
			memory = mapped;
		}

		~SignalStack()
		{
			if (!memory) return;
			stack_t current;
			if (sigaltstack(nullptr, &current) == 0 && current.ss_sp == memory)
			{
				stack_t none;
				std::memset(&none, 0, sizeof(none));
				none.ss_flags = SS_DISABLE;
				sigaltstack(&none, nullptr);
			}
			munmap(memory, kSignalStackBytes);
		}
	};

	thread_local SignalStack t_SignalStack;

	// The guard a call runs under, and the one outside it again once it returns, faults, or a C++ or C4 exception leaves
	// it (the unwinder runs this frame's cleanup for those).
	struct GuardScope
	{
		Guard* outside;
		explicit GuardScope(Guard* guard) : outside(t_Guard) { t_Guard = guard; }
		~GuardScope() { t_Guard = outside; }
	};
}

int32_t c4s_guarded_call(void (*fn)(void*), void* context, c4s_fault* fault)
{
	InstallHandlers();
	t_SignalStack.Attach();
	Guard guard = {};
	GuardScope scope(&guard);
	if (sigsetjmp(guard.jump, 1) == 0)
	{
		fn(context);
		return 1;
	}
	if (fault) *fault = guard.fault;
	return 0;
}

#endif
