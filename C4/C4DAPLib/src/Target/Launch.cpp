#include "Target/Launch.h"
#include "Target/WideText.h"
#include <memory>

namespace C4DAP::Target
{
	namespace
	{
		// The C runtime's rules: a quoted argument ends at an unescaped quote; backslashes are literal unless they
		// come before a quote, where each pair is one backslash.
		std::wstring Quoted(const std::wstring& argument)
		{
			if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
			std::wstring quoted = L"\"";
			size_t backslashes = 0;
			for (wchar_t c : argument)
			{
				if (c == L'\\') { ++backslashes; continue; }
				if (c == L'"') quoted.append(backslashes * 2 + 1, L'\\');
				else quoted.append(backslashes, L'\\');
				backslashes = 0;
				quoted += c;
			}
			quoted.append(backslashes * 2, L'\\');
			return quoted + L"\"";
		}

		std::string LastError(const char* what)
		{
			return std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")";
		}

		struct HandleList
		{
			std::unique_ptr<uint8_t[]> storage;
			LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
			~HandleList() { if (list) DeleteProcThreadAttributeList(list); }
		};

		bool InheritOnly(HandleList& attributes, HANDLE* handles, size_t count)
		{
			SIZE_T size = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
			attributes.storage = std::make_unique<uint8_t[]>(size);
			auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.storage.get());
			if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) return false;
			attributes.list = list;
			return UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, count * sizeof(HANDLE), nullptr, nullptr) != FALSE;
		}
	}

	std::wstring CommandLine(const std::string& program, const std::vector<std::string>& args)
	{
		std::wstring line = Quoted(Wide(program));
		for (const std::string& argument : args) line += L" " + Quoted(Wide(argument));
		return line;
	}

	std::string StartDebugged(const LaunchOptions& options, const OutputPipes& output)
	{
		if (GetFileAttributesW(Wide(options.program).c_str()) == INVALID_FILE_ATTRIBUTES) return "no program at " + options.program;
		SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);
		if (input == INVALID_HANDLE_VALUE) return LastError("could not open NUL for the program's input");
		HANDLE handles[] = { input, output.StdoutWriter(), output.StderrWriter() };
		HandleList attributes;
		if (!InheritOnly(attributes, handles, std::size(handles)))
		{
			CloseHandle(input);
			return LastError("could not limit the handles the program inherits");
		}
		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput = input;
		startup.StartupInfo.hStdOutput = output.StdoutWriter();
		startup.StartupInfo.hStdError = output.StderrWriter();
		startup.lpAttributeList = attributes.list;
		std::wstring commandLine = CommandLine(options.program, options.args);
		const std::wstring cwd = Wide(options.cwd);
		PROCESS_INFORMATION started{};
		const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
			DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
			&startup.StartupInfo, &started);
		const std::string failure = ok ? "" : LastError(("could not start " + options.program).c_str());
		CloseHandle(input);
		if (!ok) return failure;
		CloseHandle(started.hThread);
		CloseHandle(started.hProcess);
		return "";
	}
} // namespace C4DAP::Target
