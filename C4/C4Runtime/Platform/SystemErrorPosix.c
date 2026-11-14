// System error text (SystemErrorC.h) over strerror_r: on Linux a code is an errno value.

#include "SystemErrorC.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { kMessageBytes = 256, kCodeBytes = 32 };

// glibc's text for a code it has no message for ("Unknown error 1234"): the code is written alone then, as Windows
// writes a code FormatMessage does not know.
static const char kUnknownMessage[] = "Unknown error";

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

// The C library's message for `code` (the GNU strerror_r, which may return its own text instead of `buffer`); "" when
// it has none.
static const char* Message(uint32_t code, char* buffer, size_t capacity)
{
	if (code > INT32_MAX) return "";
	const char* message = strerror_r((int)code, buffer, capacity);
	if (!message || strncmp(message, kUnknownMessage, sizeof(kUnknownMessage) - 1) == 0) return "";
	return message;
}

void c4_system_error_text(uint32_t code, char* out, int64_t capacity)
{
	char buffer[kMessageBytes];
	const char* message = Message(code, buffer, sizeof(buffer));
	char text[kMessageBytes + kCodeBytes];
	snprintf(text, sizeof(text), "%s%s(error %lu)", message, message[0] ? " " : "", (unsigned long)code);
	c4_copy_text(text, out, capacity);
}
