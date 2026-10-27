// IED's files in Explorer (docs/27): a solution (.iedsln) and a federation (.iedfed) open in this program on a double
// click, for this user only (keys under HKEY_CURRENT_USER: no admin, no other user touched). The classes key is a
// parameter, so a test registers under a scratch key of its own.

include "Window.ch"

namespace Desktop
{
	const char* c_UserClasses = "Software\\Classes";

	// Each type under `classes` (a key below HKEY_CURRENT_USER): its extension names IED's programmatic id, which
	// names the type, takes the program's icon and opens the file with `program`.
	bool RegisterFileTypes(const FS::Path& program, Core::StringView classes, Core::String& error);
	// The programmatic ids removed, and the extensions that still name them; an extension another program took is
	// left to it.
	bool UnregisterFileTypes(Core::StringView classes, Core::String& error);
	// What opening a file of `extension` runs, as registered under `classes`; empty when nothing is.
	Core::String OpenCommand(Core::StringView extension, Core::StringView classes);
	// Explorer is told that associations changed, so icons and types show at once.
	void AssociationsChanged();
}
