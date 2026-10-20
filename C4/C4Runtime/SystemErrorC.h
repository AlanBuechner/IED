#pragma once
// Error text for the runtime's operating-system services (Platform, Net): the system's message and its code.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// `text` into `out`, cut at a whole UTF-8 character when it does not fit; always null-terminated when capacity > 0.
void c4_copy_text(const char* text, char* out, int64_t capacity);

// The system's message for `code`, without its line break, then the code: "The specified module could not be found.
// (error 126)". Written as c4_copy_text writes.
void c4_system_error_text(uint32_t code, char* out, int64_t capacity);

#ifdef __cplusplus
}
#endif
