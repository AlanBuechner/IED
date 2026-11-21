// The Linux window shell (Shell.h): GTK 3 and WebKitGTK behind a small C interface, Shell.c compiled into the program
// (C4/docs/language/13-interop.md). It loads them when the app starts, so building needs neither.
#if defined(PLATFORM_LINUX)
extern "c" include "Shell.h"
extern "c" source "Shell.c"
#endif
