// IED in the Start menu (docs/21 "Install shape"): `ied-desktop --register` writes a shortcut to this program into the
// user's Start menu (%APPDATA%\Microsoft\Windows\Start Menu\Programs\IED.lnk), so the Windows key and "ied" start it;
// `--unregister` takes it out. Per user, no administrator.

include "window.ch"

namespace Desktop
{
	// The user's Start menu folder of programs, under `appData` (%APPDATA%).
	FS::Path StartMenuFolder(Core::StringView appData);
	FS::Path ShortcutPath(Core::StringView appData);
	// The shortcut to `program`, with its icon; false with `error`.
	bool Register(const FS::Path& program, Core::StringView appData, Core::String& error);
	// No shortcut afterwards (none before is fine); false with `error`.
	bool Unregister(Core::StringView appData, Core::String& error);
}
