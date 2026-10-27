// Win32, COM, DWM and the WebView2 SDK, bound from native/webview.h by CBind (C4/docs/language/13-interop.md); the
// SDK's folder is an include folder of the project (nuke.toml).

extern "c++" include "native/webview.h"

namespace Desktop
{
	const int32 c_Ok = 0;                      // S_OK
	const int32 c_Failed = -2147467259;        // E_FAIL
	const int32 c_NoInterface = -2147467262;   // E_NOINTERFACE
	const int32 c_Cancelled = -2147023673;     // HRESULT_FROM_WIN32(ERROR_CANCELLED): the person closed a dialog
}
