#pragma once
#include <string>

namespace C4DAP::Target
{
	/// UTF-8 (the protocol's text) to UTF-16 (the Windows API's) and back.
	std::wstring Wide(const std::string& text);
	std::string Narrow(const wchar_t* text);
	inline std::string Narrow(const std::wstring& text) { return Narrow(text.c_str()); }
} // namespace C4DAP::Target
