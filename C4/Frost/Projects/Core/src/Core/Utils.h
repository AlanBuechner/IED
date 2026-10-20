#pragma once
#include "Core/Types.h"
#include <wchar.h>
#include <cstring>
#include <stdlib.h>
#include <string>

namespace Core
{
	const wchar_t* GetWStr(const char* str);
	const char* GetStr(const wchar_t* wstr);

	inline std::wstring GetWStr(const std::string& str) { return { str.begin(), str.end() }; }
	inline std::string GetStr(const std::wstring& wstr) { return { wstr.begin(), wstr.end() }; }

	// https://retroscience.net/brents-c-programming-rules.html

	inline uint8 utf8charlen(const char* str) {
		uint32 v01 = ((*str & 0x80) >> 7) & ((*str & 0x40) >> 6);
		uint32 v2 = (*str & 0x20) >> 5;
		uint32 v3 = (*str & 0x10) >> 4;
		return 1 + ((v01 << v2) | (v01 & v3));
	}

	inline uint32 utf8len(const char* str) {
		uint32 len = 0;
		for (uint32 i = 0; *str != 0; ++len)
			str += utf8charlen(str);
		return len;
	}

	inline bool utf8valid(const char* const str) {
		if (str == NULL)
			return false;
		const char* c = str;
		bool valid = true;
		for (uint32 i = 0; c[0] != 0 && valid;) {
			valid = (c[0] & 0x80) == 0
				|| ((c[0] & 0xE0) == 0xC0 && (c[1] & 0xC0) == 0x80)
				|| ((c[0] & 0xF0) == 0xE0 && (c[1] & 0xC0) == 0x80 && (c[2] & 0xC0) == 0x80)
				|| ((c[0] & 0xF8) == 0xF0 && (c[1] & 0xC0) == 0x80 && (c[2] & 0xC0) == 0x80 && (c[3] & 0xC0) == 0x80);
			uint32 v01 = ((*str & 0x80) >> 7) & ((*str & 0x40) >> 6);
			uint32 v2 = (c[0] & 0x20) >> 5;
			uint32 v3 = (c[0] & 0x10) >> 4;
			i += 1 + ((v01 << v2) | (v01 & v3));
			c = str + i;
		}
		return valid;
	}

	inline char32 GetNextChar32(const char* const str, uint32* offset) {
		uint8 length = utf8charlen(&str[*offset]);
		char32 c32 = 0;
		for (char* c = (char*)&c32; c < ((char*)&c32) + length; c++, (*offset)++)
			*c = str[*offset];
		return c32;
	}

	inline char32 GetNextChar32(const char* const str, uint32 offset = 0) {
		return GetNextChar32(str, &offset);
	}

	inline char32 GetNextChar32(const char** const str) {
		uint32 offset = 0;
		char32 c = GetNextChar32(*str, &offset);
		*str += offset;
		return c;
	}

}
