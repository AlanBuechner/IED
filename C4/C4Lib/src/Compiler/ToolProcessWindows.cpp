#include "Compiler/BuildUtils.h"

#if defined(_WIN32)

#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace Scripting
{
	bool RunTool(const std::vector<std::string>& args)
	{
		std::wstring cmdLine;
		for (size_t i = 0; i < args.size(); ++i)
		{
			if (i > 0) cmdLine += L" ";
			cmdLine += L"\"";
			for (unsigned char c : args[i])
				cmdLine += (wchar_t)c;
			cmdLine += L"\"";
		}

		std::wstring mutableCmd = cmdLine;

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
		{
			std::cerr << "error: Failed to start process (code " << GetLastError() << ")" << std::endl;
			return false;
		}

		CloseHandle(pi.hThread);
		DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
		if (waitResult == WAIT_FAILED)
		{
			std::cerr << "error: Failed to wait for process" << std::endl;
			CloseHandle(pi.hProcess);
			return false;
		}

		DWORD exitCode = 0;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);

		if (exitCode != 0)
		{
			std::cerr << "error: Process failed with exit code " << exitCode << std::endl;
			return false;
		}
		return true;
	}

	bool RunToolWithOutput(const Core::Vector<Core::String>& args, int& exitCode,
		std::string& outStdout, std::string& outStderr)
	{
		outStdout.clear();
		outStderr.clear();
		exitCode = -1;

		HANDLE outRead = INVALID_HANDLE_VALUE, outWrite = INVALID_HANDLE_VALUE;
		HANDLE errRead = INVALID_HANDLE_VALUE, errWrite = INVALID_HANDLE_VALUE;
		SECURITY_ATTRIBUTES sa = {};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		if (!CreatePipe(&outRead, &outWrite, &sa, 0)) { outStderr = "FAIL pipe1"; return false; }
		if (!CreatePipe(&errRead, &errWrite, &sa, 0)) { CloseHandle(outRead); CloseHandle(outWrite); outStderr = "FAIL pipe2"; return false; }

		std::wstring cmdLine;
		for (size_t i = 0; i < args.Count(); ++i)
		{
			if (i > 0) cmdLine += L" ";
			cmdLine += L"\"";
			for (unsigned char c : std::string(args[i].c_str()))
				cmdLine += (wchar_t)c;
			cmdLine += L"\"";
		}
		std::wstring mutableCmd = cmdLine;

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = outWrite;
		si.hStdError = errWrite;

		PROCESS_INFORMATION pi = {};
		DWORD spawnErr = 0;
		if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi))
		{
			spawnErr = GetLastError();
			CloseHandle(outRead); CloseHandle(outWrite);
			CloseHandle(errRead); CloseHandle(errWrite);
			std::string narrow;
			for (wchar_t c : cmdLine) narrow += (char)c;
			outStderr = "CreateProcessW failed (Win32 error " + std::to_string(spawnErr) + ") cmd=" + narrow;
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(outWrite);
		CloseHandle(errWrite);

		auto readAll = [](HANDLE h, std::string& out) {
			char buf[4096]; DWORD n = 0;
			while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
				out.append(buf, n);
		};
		readAll(outRead, outStdout);
		readAll(errRead, outStderr);
		CloseHandle(outRead);
		CloseHandle(errRead);

		DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD code = 0;
		if (waitResult != WAIT_FAILED)
			GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		exitCode = static_cast<int>(code);
		return true;
	}
} // namespace Scripting

#endif
