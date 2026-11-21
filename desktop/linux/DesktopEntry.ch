// IED in the Linux desktop's applications menu (docs/27 "Linux"): `ied-desktop --register` writes, for this user only
// (the freedesktop.org folders under ~/.local/share and ~/.config, no root):
//   applications/ied.desktop                       the menu's entry, starting this program (and opening IED's files)
//   icons/hicolor/scalable/apps/ied.svg, 256x256/apps/ied.png   its icon, from beside the program
//   mime/packages/ied.xml                          .iedsln and .iedfed as types of their own
//   mimeapps.list (config)                         those types opening in IED by default
// `--unregister` takes them out again. Plain files, so the texts and the edits are checked on every system.

include "bedrock.ch"

namespace Desktop
{
	const char* c_EntryName = "ied.desktop";
	const char* c_SolutionType = "application/x-ied-solution";
	const char* c_FederationType = "application/x-ied-federation";

	// $XDG_DATA_HOME, else ~/.local/share; $XDG_CONFIG_HOME, else ~/.config.
	FS::Path DataHome();
	FS::Path ConfigHome();

	// The entry for `program`; `icon` is the icon's file, or "" for the theme's "ied".
	Core::String DesktopEntryText(const FS::Path& program, Core::StringView icon);
	// One argument of an entry's Exec line, quoted as the Desktop Entry spec says (then escaped as its strings are).
	Core::String ExecArgument(Core::StringView argument);
	// The shared-mime-info package naming IED's two file types.
	Core::String MimePackageXml();
	// mimeapps.list's text with IED's types opening in ied.desktop (`set`) or no longer naming it (not `set`); the
	// rest of the file as it was.
	Core::String WithDefaults(Core::StringView text, bool set);

	// The entry, icon, types and defaults under `dataHome` and `configHome`; false with `error`.
	bool RegisterDesktopEntry(const FS::Path& program, const FS::Path& dataHome, const FS::Path& configHome, Core::String& error);
	// None of them afterwards (none before is fine); false with `error`.
	bool UnregisterDesktopEntry(const FS::Path& dataHome, const FS::Path& configHome, Core::String& error);
	// The desktop's caches told (update-mime-database, update-desktop-database, where the system has them).
	void DesktopEntriesChanged(const FS::Path& dataHome);
}
