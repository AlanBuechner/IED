// C4 strings are UTF-8; Windows takes text in UTF-16, so every path, name or value the runtime hands the OS goes
// through here (FileSystemRuntimeC.c, IORuntimeC.c, Platform/).
#pragma once

#ifdef _WIN32
#include <stdint.h>
#include <wchar.h>

// `text` (UTF-8) as a malloc'd, null-terminated UTF-16 string; NULL when it is not valid UTF-8. Free it.
wchar_t* c4_utf8_to_wide(const char* text);
// `wide` as UTF-8 into `out`: the UTF-8 length, written (null-terminated) only when it fits in `cap`; -1 when the
// text cannot be converted.
int64_t c4_wide_to_utf8(const wchar_t* wide, char* out, int64_t cap);
#endif
