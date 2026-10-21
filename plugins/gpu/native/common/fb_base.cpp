#include "fb_base.h"

#include <windows.h>
#include <cstdio>
#include <ctime>

namespace fb
{
	std::string FormatV(const char* format, va_list args) {
		va_list copy;
		va_copy(copy, args);
		const int needed = vsnprintf(nullptr, 0, format, copy);
		va_end(copy);
		if (needed <= 0) return std::string();
		std::string text((size_t)needed, '\0');
		vsnprintf(text.data(), (size_t)needed + 1, format, args);
		return text;
	}

	std::string Format(const char* format, ...) {
		va_list args;
		va_start(args, format);
		std::string text = FormatV(format, args);
		va_end(args);
		return text;
	}

	std::wstring Widen(const std::string& text) {
		if (text.empty()) return std::wstring();
		const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring wide((size_t)count, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), count);
		return wide;
	}

	std::string Narrow(const wchar_t* text) {
		if (!text || !*text) return std::string();
		const int length = (int)wcslen(text);
		const int count = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
		std::string narrow((size_t)count, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text, length, narrow.data(), count, nullptr, nullptr);
		return narrow;
	}

	std::string Narrow(const std::wstring& text) { return Narrow(text.c_str()); }

	static void (*g_logSink)(const char*) = nullptr;

	void SetLogSink(void (*sink)(const char* line)) { g_logSink = sink; }

	void Log(const char* format, ...) {
		va_list args;
		va_start(args, format);
		std::string line = "[flashbang] " + FormatV(format, args);
		va_end(args);
		if (g_logSink) g_logSink(line.c_str());
		line += "\n";
		OutputDebugStringA(line.c_str());
	}

	std::string TimeStamp() {
		SYSTEMTIME now;
		GetLocalTime(&now);
		return Format("%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	}

	std::wstring ExecutablePath() {
		std::wstring path(MAX_PATH, L'\0');
		for (;;) {
			const DWORD length = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
			if (length < path.size()) {
				path.resize(length);
				return path;
			}
			path.resize(path.size() * 2);
		}
	}

	std::string ExecutableName() {
		std::wstring path = ExecutablePath();
		size_t slash = path.find_last_of(L"\\/");
		std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
		size_t dot = name.find_last_of(L'.');
		if (dot != std::wstring::npos) name.resize(dot);
		return Narrow(name);
	}
}
