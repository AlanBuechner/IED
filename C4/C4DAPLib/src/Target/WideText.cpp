#include "Target/WideText.h"
#include <windows.h>

namespace C4DAP::Target
{
	std::wstring Wide(const std::string& text)
	{
		if (text.empty()) return {};
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring wide(static_cast<size_t>(length), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
		return wide;
	}

	std::string Narrow(const wchar_t* text)
	{
		if (!text || !*text) return {};
		const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		std::string narrow(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), length, nullptr, nullptr);
		narrow.pop_back(); // the terminator WideCharToMultiByte counted
		return narrow;
	}
} // namespace C4DAP::Target
