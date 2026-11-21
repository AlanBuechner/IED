// Windows: Win32 and WebView2. The Linux app is desktop/linux (docs/27 "Linux").
#if defined(PLATFORM_WINDOWS)
// File > Open Folder's picker: the shell's folder dialog (IFileOpenDialog in folder mode), modal to an IED window.

include "Window.ch"

namespace Desktop
{
	// The folder the person picked; false when they cancelled (`error` stays empty) or the dialog failed (`error`).
	bool PickFolder(HWND__* owner, Core::String& folder, Core::String& error);
}
#endif
