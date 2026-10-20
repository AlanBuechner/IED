#include "Target/Engine.h"
#include "Target/WideText.h"

namespace C4DAP::Target
{
	namespace
	{
		// The runtime reports an exception nothing caught through this function (C4Runtime/EHRuntime.cpp).
		const char* const kUncaughtHook = "c4_report_uncaught_exception";
		// The C4 program's own `main` (the executable's entry point is a wrapper without lines).
		const char* const kEntryFunction = "main";
		constexpr DWORD kStartWaitMilliseconds = 30000;
		constexpr UINT kTerminatedExitCode = 1;
		constexpr size_t kMaxDebugStringBytes = 4096;

		uint64_t BreakInStart()
		{
			const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			return ntdll ? reinterpret_cast<uint64_t>(GetProcAddress(ntdll, "DbgUiRemoteBreakin")) : 0;
		}
	}

	Engine::Engine(EngineEvents events) : m_Events(std::move(events)), m_BreakInStart(BreakInStart()) {}

	Engine::~Engine()
	{
		Terminate();
	}

	std::string Engine::Launch(const LaunchOptions& options)
	{
		if (m_State != State::Idle) return "a program is already running";
		m_Output = std::make_unique<OutputPipes>();
		if (!m_Output->Open()) return "could not create the program's output pipes";
		const std::string failure = StartDebugged(options, *m_Output);
		if (!failure.empty()) return failure;
		m_Output->StartForwarding(m_Events.output);
		m_StopOnEntry = options.stopOnEntry;
		m_State = State::Running;
		// Hold the program at the loader's breakpoint: its code is loaded and breakpoints can bind.
		const auto deadline = GetTickCount64() + kStartWaitMilliseconds;
		while (m_State == State::Running && !m_StartReached && GetTickCount64() < deadline) Poll(kStartWaitMilliseconds);
		if (m_State == State::Ended) return "the program ended before it started (exit code " + std::to_string(m_ExitCode) + ")";
		if (!m_StartReached) return "the program did not start";
		if (m_Configured) Configured();
		return "";
	}

	void Engine::Configured()
	{
		m_Configured = true;
		if (m_State != State::Stopped || !m_StartReached) return;
		if (m_StopOnEntry)
			for (uint64_t entry : m_Symbols.FunctionsNamed(kEntryFunction))
				if (m_Patches.Add(m_Process, entry)) { m_EntryPatch = entry; break; }
		Resume(m_Held.dwThreadId, false);
	}

	std::vector<BoundBreakpoint> Engine::SetBreakpoints(const std::string& path, const std::vector<uint32_t>& lines)
	{
		return m_Breakpoints.Set(path, lines, m_SymbolsLoaded ? &m_Symbols : nullptr, m_Process, m_Patches);
	}

	void Engine::SetExceptionFilters(bool thrown, bool uncaught)
	{
		m_StopOnThrown = thrown;
		m_StopOnUncaught = uncaught;
		ArmUncaughtHook();
	}

	void Engine::ArmUncaughtHook()
	{
		if (!m_SymbolsLoaded) return;
		if (m_StopOnUncaught && !m_UncaughtPatch)
		{
			if (const std::optional<uint64_t> hook = m_Symbols.PublicFunction(kUncaughtHook))
				if (m_Patches.Add(m_Process, *hook)) m_UncaughtPatch = *hook;
		}
		else if (!m_StopOnUncaught && m_UncaughtPatch)
		{
			m_Patches.Release(m_Process, m_UncaughtPatch);
			m_UncaughtPatch = 0;
		}
	}

	void Engine::Terminate()
	{
		if (m_State == State::Idle || m_State == State::Ended) return;
		m_Terminating = true;
		TerminateProcess(m_Process.Handle(), kTerminatedExitCode);
		if (m_State == State::Stopped)
		{
			ContinueDebugEvent(m_Held.dwProcessId, m_Held.dwThreadId, DBG_CONTINUE);
			m_State = State::Running;
		}
		while (m_State == State::Running) Poll(INFINITE);
	}

	void Engine::Poll(DWORD milliseconds)
	{
		DEBUG_EVENT event{};
		if (WaitForDebugEventEx(&event, milliseconds)) Handle(event);
	}

	void Engine::Handle(const DEBUG_EVENT& event)
	{
		switch (event.dwDebugEventCode)
		{
		case CREATE_PROCESS_DEBUG_EVENT: ProcessCreated(event); break;
		case CREATE_THREAD_DEBUG_EVENT: ThreadCreated(event); break;
		case EXIT_THREAD_DEBUG_EVENT:
			m_Process.ThreadEnded(event.dwThreadId);
			if (!m_BreakInThreads.erase(event.dwThreadId)) m_Events.thread(event.dwThreadId, false);
			PassOn(event, DBG_CONTINUE);
			break;
		case LOAD_DLL_DEBUG_EVENT:
			m_Symbols.ModuleLoaded(event.u.LoadDll.hFile, reinterpret_cast<uint64_t>(event.u.LoadDll.lpBaseOfDll));
			if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
			PassOn(event, DBG_CONTINUE);
			break;
		case UNLOAD_DLL_DEBUG_EVENT:
			m_Symbols.ModuleUnloaded(reinterpret_cast<uint64_t>(event.u.UnloadDll.lpBaseOfDll));
			PassOn(event, DBG_CONTINUE);
			break;
		case OUTPUT_DEBUG_STRING_EVENT: DebugOutput(event); break;
		case EXCEPTION_DEBUG_EVENT: ExceptionRaised(event); break;
		case EXIT_PROCESS_DEBUG_EVENT: ProcessExited(event); break;
		default: PassOn(event, DBG_CONTINUE); break;
		}
	}

	void Engine::ProcessCreated(const DEBUG_EVENT& event)
	{
		const CREATE_PROCESS_DEBUG_INFO& info = event.u.CreateProcessInfo;
		m_Process.Started(info, event.dwProcessId, event.dwThreadId);
		if (m_Symbols.Open(info.hProcess))
		{
			m_Symbols.ModuleLoaded(info.hFile, reinterpret_cast<uint64_t>(info.lpBaseOfImage));
			m_SymbolsLoaded = true;
		}
		if (info.hFile) CloseHandle(info.hFile);
		m_Events.thread(event.dwThreadId, true);
		if (m_SymbolsLoaded)
		{
			for (const BoundBreakpoint& breakpoint : m_Breakpoints.BindPending(m_Symbols, m_Process, m_Patches))
				m_Events.breakpointChanged(breakpoint);
			ArmUncaughtHook();
		}
		PassOn(event, DBG_CONTINUE);
	}

	void Engine::ThreadCreated(const DEBUG_EVENT& event)
	{
		m_Process.ThreadStarted(event.dwThreadId, event.u.CreateThread.hThread);
		if (reinterpret_cast<uint64_t>(event.u.CreateThread.lpStartAddress) == m_BreakInStart) m_BreakInThreads.insert(event.dwThreadId);
		else m_Events.thread(event.dwThreadId, true);
		PassOn(event, DBG_CONTINUE);
	}

	void Engine::ProcessExited(const DEBUG_EVENT& event)
	{
		m_ExitCode = event.u.ExitProcess.dwExitCode;
		ClearStopTables();
		m_Step.reset();
		m_Symbols.Close();
		m_SymbolsLoaded = false;
		m_Process.Ended();
		ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
		m_State = State::Ended;
		if (m_Output) m_Output->Finish(); // the program's last output goes out before it is reported ended
		m_Events.exited(m_ExitCode);
	}

	void Engine::DebugOutput(const DEBUG_EVENT& event)
	{
		const OUTPUT_DEBUG_STRING_INFO& info = event.u.DebugString;
		const uint64_t address = reinterpret_cast<uint64_t>(info.lpDebugStringData);
		std::string text;
		if (info.fUnicode)
		{
			std::wstring wide(std::min<size_t>(info.nDebugStringLength, kMaxDebugStringBytes / sizeof(wchar_t)), L'\0');
			if (m_Process.Read(address, wide.data(), wide.size() * sizeof(wchar_t))) text = Narrow(wide.c_str());
		}
		else
		{
			bool complete = false;
			text = m_Process.ReadText(address, kMaxDebugStringBytes, complete);
		}
		if (!text.empty()) m_Events.output("console", text);
		PassOn(event, DBG_CONTINUE);
	}

	void Engine::PassOn(const DEBUG_EVENT& event, DWORD status)
	{
		ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
	}
} // namespace C4DAP::Target
