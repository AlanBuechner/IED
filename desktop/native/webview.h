// What ied-desktop binds through CBind (C4/docs/language/13-interop.md): Win32 windows, COM, DWM and the WebView2 SDK.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>
// The SDK by its path from here until the compiler hands the project's include folders to CBind (reported to the C4
// session, 2026-10-21); then plainly "WebView2.h" from nuke.toml's `includes`.
#include "../../vendor/webview2/build/native/include/WebView2.h"
