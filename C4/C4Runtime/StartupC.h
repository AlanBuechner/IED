#pragma once
// A runtime function the C runtime runs on the main thread before any of the program's constructors: an initializer in
// Windows' .CRT$XIU section, the earliest constructor priority on ELF. `fn` is `static int fn(void)`.

#if defined(_WIN32)
#define C4_AT_STARTUP(fn) __attribute__((section(".CRT$XIU"), used)) static int (*const fn##_at_startup)(void) = fn
#else
#define C4_AT_STARTUP(fn) __attribute__((constructor(101))) static void fn##_at_startup(void) { (void)fn(); }
#endif
