// fb_base.h - what every Flashbang source uses: fixed-size integers, text conversions and the log.
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <memory>

namespace fb
{
	using u8 = uint8_t;
	using u16 = uint16_t;
	using u32 = uint32_t;
	using u64 = uint64_t;
	using i32 = int32_t;
	using i64 = int64_t;

	std::string Format(const char* format, ...);
	std::string FormatV(const char* format, va_list args);

	std::wstring Widen(const std::string& text);
	std::string Narrow(const std::wstring& text);
	std::string Narrow(const wchar_t* text);

	// A line for whoever listens: the debugger output, and the sink a program set (the replayer's stderr).
	void Log(const char* format, ...);
	void SetLogSink(void (*sink)(const char* line));

	// The current time as "yyyymmdd-hhmmss" (local).
	std::string TimeStamp();
	// The full path of the running executable, and its file name without folder or extension.
	std::wstring ExecutablePath();
	std::string ExecutableName();

	inline u64 AlignUp(u64 value, u64 alignment) { return (value + alignment - 1) / alignment * alignment; }
}
