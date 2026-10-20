// windows.ch - the Win32 key codes the SDK does not define as macros, for C4 programs that bind <windows.h>.
//
// CBind binds the SDK's declarations and the object-like macros that are constants (WM_CLOSE, WS_OVERLAPPED,
// INFINITE, ...; BUG-284), so they come with the `extern "c++" include` of the headers. The letter keys are not
// macros in the SDK ("VK_A - VK_Z are the same as ASCII 'A' - 'Z'"), so they are written here.
//
// Include from C4 source:  include "windows.ch"

const int32 VK_W = 0x57;
const int32 VK_A = 0x41;
const int32 VK_S = 0x53;
const int32 VK_D = 0x44;
