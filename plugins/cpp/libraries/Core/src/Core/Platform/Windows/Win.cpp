#include "Win.h"
#include <cassert>
#include <ShellAPI.h>

std::wstring TranslateError(HRESULT hr)
{
	wchar_t* pMsgBuf = nullptr;
	DWORD nMsgLen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&pMsgBuf), 0, nullptr);
	std::wstring errorString = pMsgBuf;
	if (nMsgLen == 0)
		errorString = L"Unidentified error code";
	LocalFree(pMsgBuf);
	return errorString;
}

#if defined(PLATFORM_WINDOWS)

namespace Platform
{
	fs::path GetApplicationPath()
	{
		wchar_t p[MAX_PATH] = { 0 };
		if(p[0] == 0)
			GetModuleFileNameW(NULL, p, MAX_PATH);
		return fs::path(p);
	}

	void* LoadDynamicLibrary(const wchar_t* path)
	{
		HMODULE h = ::LoadLibraryW(path);
		assert(h != nullptr);
		return (void*)h;

	}
	void* GetLibraryExport(void* h, const char* name)
	{
		void* f = (void*)::GetProcAddress((HMODULE)h, name);
		assert(f != nullptr);
		return f;
	}

	void FreeDynamicLibrary(void* h)
	{
		if (h != nullptr)
			::FreeLibrary((HMODULE)h);
	}


	int SystemCall(const Core::String& cmd, const fs::path& dir, bool supress)
	{
		// create output read and write pipes
		HANDLE childStd_OUT_Read = NULL;
		HANDLE childStd_OUT_Write = NULL;

		if (supress == false)
		{
			SECURITY_ATTRIBUTES saAttr;
			saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
			saAttr.bInheritHandle = TRUE;
			saAttr.lpSecurityDescriptor = NULL;
			if (!CreatePipe(&childStd_OUT_Read, &childStd_OUT_Write, &saAttr, 0))
				return -1;

			if (!SetHandleInformation(childStd_OUT_Read, HANDLE_FLAG_INHERIT, 0))
				return -1;
		}

		// Set up members of the STARTUPINFO structure.
		STARTUPINFO siStartInfo;
		ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
		siStartInfo.cb = sizeof(STARTUPINFO);
		siStartInfo.hStdError = childStd_OUT_Write;
		siStartInfo.hStdOutput = childStd_OUT_Write;
		siStartInfo.hStdInput = nullptr;
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

		// Set up members of the PROCESS_INFORMATION structure.
		PROCESS_INFORMATION piProcInfo;
		ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

		// Create the child process.
		BOOL bSuccess = CreateProcess(NULL,
			Core::GetWStr(cmd).data(),     // command line
			NULL,          // process security attributes
			NULL,          // primary thread security attributes
			TRUE,          // handles are inherited
			0,             // creation flags
			NULL,          // use parent's environment
			(dir == "") ? NULL : dir.c_str(),
			&siStartInfo,  // STARTUPINFO pointer
			&piProcInfo);  // receives PROCESS_INFORMATION

		// read the output
		if (supress == false)
		{
			// close write pipe
			if (childStd_OUT_Write != 0)
				CloseHandle(childStd_OUT_Write);

#define BUFSIZE 4096
			DWORD dwRead, dwWritten;
			CHAR chBuf[BUFSIZE];
			BOOL bSuccess = FALSE;
			HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

			for (;;)
			{
				bSuccess = ReadFile(childStd_OUT_Read, chBuf, BUFSIZE, &dwRead, NULL);
				if (!bSuccess || dwRead == 0) break;

				bSuccess = WriteFile(hParentStdOut, chBuf,
					dwRead, &dwWritten, NULL);
				if (!bSuccess) break;
			}
#undef BUFSIZE

			// close read pipe
			if (childStd_OUT_Read != 0)
				CloseHandle(childStd_OUT_Read);
		}

		// clean up and return
		if (bSuccess)
		{
			DWORD code = 0;
			GetExitCodeProcess(piProcInfo.hProcess, &code);

			CloseHandle(piProcInfo.hProcess);
			CloseHandle(piProcInfo.hThread);

			return code;
		}

		return GetLastError();
	}

	bool ClipboardHasText()
	{
		if (!OpenClipboard(nullptr))
			return false;

		bool hasText = IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;

		CloseClipboard();
		return hasText;
	}

	Core::String ClipboardGetText()
	{
		if (!OpenClipboard(nullptr))
			return {};

		if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
		{
			CloseClipboard();
			return {};
		}

		HGLOBAL hGlobal = GetClipboardData(CF_UNICODETEXT);
		if (hGlobal == nullptr)
		{
			CloseClipboard();
			return {};
		}

		wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hGlobal));
		if (wstr == nullptr)
		{
			CloseClipboard();
			return {};
		}

		// Convert UTF-16 to UTF-8; GetStr allocates with new[] so caller must delete[]
		const char* utf8 = Core::GetStr(wstr);
		Core::String result(utf8);
		delete[] utf8;

		GlobalUnlock(hGlobal);
		CloseClipboard();

		return result;
	}

	void ClipboardSetText(const Core::String& text)
	{
		if (!OpenClipboard(nullptr))
			return;

		EmptyClipboard();

		// Convert UTF-8 to UTF-16; GetWStr allocates with new[] so caller must delete[]
		const wchar_t* wstr = Core::GetWStr(text.c_str());

		// Allocate movable global memory for clipboard data
		size_t byteCount = (wcslen(wstr) + 1) * sizeof(wchar_t);
		HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, byteCount);

		if (hGlobal == nullptr)
		{
			delete[] wstr;
			CloseClipboard();
			return;
		}

		wchar_t* dest = static_cast<wchar_t*>(GlobalLock(hGlobal));
		if (dest == nullptr)
		{
			GlobalFree(hGlobal);
			delete[] wstr;
			CloseClipboard();
			return;
		}

		memcpy(dest, wstr, byteCount);
		GlobalUnlock(hGlobal);
		delete[] wstr;

		SetClipboardData(CF_UNICODETEXT, hGlobal);
		CloseClipboard();
	}

	std::wstring Utf8ToWide(const char* utf8)
	{
		if (utf8 == nullptr)
			return {};

		const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
		if (wideLen <= 0)
			return {};

		std::wstring result(wideLen - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], wideLen);
		return result;
	}

	Core::String WideToUtf8(const wchar_t* wide)
	{
		if (wide == nullptr)
			return {};

		const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (utf8Len <= 0)
			return {};

		Core::String result(utf8Len - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], utf8Len, nullptr, nullptr);
		return result;
	}

	std::string ExecuteProcess(const std::string& a_ExePath, const std::vector<std::string>& a_Args)
	{
		std::string result;

		// Build the full command line: "exe" arg1 arg2 ...
		// Always quote the executable path to handle spaces in paths.
		std::wstring cmdLine = L"\"";
		cmdLine += Utf8ToWide(a_ExePath.c_str());
		cmdLine += L"\"";
		for (const auto& arg : a_Args)
		{
			cmdLine += L' ';
			// Quote arguments that contain spaces or tabs, or don't start with -
			bool needsQuotes = !arg.empty() && arg.front() != '-' &&
				(arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos);
			if (needsQuotes)
			{
				cmdLine += L'"';
				cmdLine += Utf8ToWide(arg.c_str());
				cmdLine += L'"';
			}
			else
			{
				cmdLine += Utf8ToWide(arg.c_str());
			}
		}

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.dwFlags |= STARTF_USESTDHANDLES;

		// Create anonymous pipe for stdout capture
		HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
		if (!CreatePipe(&hReadPipe, &hWritePipe, nullptr, 0))
		{
			return result;
		}

		si.hStdOutput = hWritePipe;
		si.hStdError = hWritePipe; // merge stderr into stdout pipe

		PROCESS_INFORMATION pi{};
		BOOL ok = CreateProcessW(
			nullptr,                    // application name (use cmd line instead)
			cmdLine.data(),             // command line (modifiable)
			nullptr, nullptr,           // no security attrs
			true,                       // inherit handles
			CREATE_NO_WINDOW,           // don't create console window for child
			nullptr, nullptr,           // use parent env and cwd
			&si, &pi
		);

		CloseHandle(hWritePipe); // parent doesn't need write end

		if (!ok)
		{
			DWORD err = GetLastError();
			CORE_WARN("ExecuteProcess: CreateProcessW failed for \"{0}\" with error {1}: {2}",
				a_ExePath, err, TranslateError(HRESULT_FROM_WIN32(err)));
			CloseHandle(hReadPipe);
			return result;
		}

		// Read all output from the pipe
		char buffer[4096];
		DWORD bytesRead = 0;
		while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
		{
			result.append(buffer, bytesRead);
		}

		CloseHandle(hReadPipe);
		WaitForSingleObject(pi.hProcess, INFINITE);

		DWORD exitCode = 0;
		GetExitCodeProcess(pi.hProcess, &exitCode);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		return result;
	}

}

#endif
