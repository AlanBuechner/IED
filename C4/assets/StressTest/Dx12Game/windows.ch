// windows.ch - Win32 named constants for C4 programs that interop with <windows.h>.
//
// Rationale: CBind binds the real C/C++ declarations (structs, enums, COM interfaces,
// functions) from the SDK headers automatically, but it deliberately does NOT import the
// hundreds of #define macros those headers contain - most are junk or incompatible
// expansions. This header instead defines, by hand and with values verified against the
// Windows SDK (10.0.26100), only the small set of named constants that C4 code actually
// uses. Add a constant here when your interop code needs one; do not bulk-import macros.
//
// Include from C4 source:  include "windows.ch"
// (The matching C/C++ declarations still come from an `extern "c++" include "<headers>"`.)

// ---- window messages (winuser.h) ----
const int32 WM_CLOSE   = 0x0010;
const int32 WM_DESTROY = 0x0002;
const int32 WM_SIZE    = 0x0005;
const int32 WM_QUIT    = 0x0012;
const int32 WM_KEYDOWN = 0x0100;
const int32 WM_KEYUP   = 0x0101;

// ---- virtual-key codes (winuser.h) ----
// VK_ESCAPE = 0x1B; the letter keys are plain ASCII per the SDK ("VK_A - VK_Z are the
// same as ASCII 'A' - 'Z'").
const int32 VK_ESCAPE = 0x1B;
const int32 VK_W      = 0x57;
const int32 VK_A      = 0x41;
const int32 VK_S      = 0x53;
const int32 VK_D      = 0x44;

// ---- window class / creation styles (winuser.h) ----
const int32 CS_HREDRAW          = 0x0002;
// WS_OVERLAPPED(0) | WS_CAPTION(0xC00000) | WS_SYSMENU(0x80000) | WS_THICKFRAME(0x40000)
// | WS_MINIMIZEBOX(0x20000) | WS_MAXIMIZEBOX(0x10000)
const int32 WS_OVERLAPPEDWINDOW = 0x00CF0000;

// ---- ShowWindow commands (winuser.h) ----
const int32 SW_SHOW = 5;

// ---- wait timeout (winbase.h): INFINITE = 0xFFFFFFFF ----
const int32 INFINITE = 0xFFFFFFFF;
