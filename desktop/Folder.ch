// File > Open Folder's picker: the shell's folder dialog (IFileOpenDialog in folder mode), modal to an IED window.

include "Window.ch"

namespace Desktop
{
	// The folder the person picked; false when they cancelled (`error` stays empty) or the dialog failed (`error`).
	bool PickFolder(HWND__* owner, Core::String& folder, Core::String& error);
}
