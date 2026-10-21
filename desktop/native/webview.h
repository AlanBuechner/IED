// What ied-desktop binds through CBind (C4/docs/language/13-interop.md): Win32 windows, COM, DWM and the WebView2 SDK
// (from nuke.toml's `includes`).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include "WebView2.h"
