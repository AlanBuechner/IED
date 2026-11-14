// A workspace's own .ied/ folder (docs/07 "Workspace state", docs/features/03): what goes with the folder and is
// committed with it - its skills, settings, plugins. A folder or a solution the owner opens gets .ied/skills/ at once, so
// the owner and the AI see where the workspace's own skills go; its per-machine state is LocalState.ch's (a folder's in
// the app data).

include "bedrock.ch"

namespace Ied
{
	// Makes <root>/.ied/ and the folders it starts with (skills/), each only when missing: what could not be made, one
	// line each.
	Core::Array<Core::String> MakeIedFolder(const FS::Path& root);
}
