#pragma once
#include "Core/Common.h"
#include "Core/String.h"
#include <vector>

namespace Platform
{
	fs::path GetApplicationPath();
	inline fs::path GetApplicationFolder() { static const fs::path folder = GetApplicationPath().parent_path(); return folder; }

	void* LoadDynamicLibrary(const wchar_t* path);
	void* GetLibraryExport(void* h, const char* name);
	void FreeDynamicLibrary(void* h);

	int SystemCall(const Core::String& cmd, const fs::path& dir = "", bool supress = false);

	std::string ExecuteProcess(const std::string& a_ExePath, const std::vector<std::string>& a_Args);

	bool ClipboardHasText();
	Core::String ClipboardGetText();
	void ClipboardSetText(const Core::String& text);

	std::wstring Utf8ToWide(const char* utf8);
	Core::String WideToUtf8(const wchar_t* wide);
}