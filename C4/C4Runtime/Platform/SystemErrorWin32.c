// System error text (SystemErrorC.h) over FormatMessageW.

#include "SystemErrorC.h"
#include "PathRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

enum { kMessageUnits = 512 };

void c4_copy_text(const char* text, char* out, int64_t capacity)
{
	if (!out || capacity <= 0) return;
	int64_t length = (int64_t)strlen(text);
	if (length >= capacity)
	{
		length = capacity - 1;
		while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) --length;
	}
	memcpy(out, text, (size_t)length);
	out[length] = 0;
}

void c4_system_error_text(uint32_t code, char* out, int64_t capacity)
{
	wchar_t message[kMessageUnits];
	DWORD units = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, message, kMessageUnits, NULL);
	while (units > 0 && (message[units - 1] == L'\r' || message[units - 1] == L'\n' || message[units - 1] == L' ')) --units;
	message[units] = 0;
	char text[kMessageUnits * 3 + 32];
	const int64_t length = c4_wide_to_utf8(message, text, sizeof(text));
	snprintf(text + (length > 0 ? length : 0), 32, "%s(error %lu)", length > 0 ? " " : "", (unsigned long)code);
	c4_copy_text(text, out, capacity);
}
