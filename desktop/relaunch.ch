// Relaunch onto the newest install (docs/27 "Relaunch"): the app writes its windows - where each is and the workspace
// it shows - to a file, stops its server, starts its own program again with `--relaunch <file>` and exits; the new
// process waits for the old one to be gone, then opens those windows again on a server of the new build.

include "placement.ch"

namespace Desktop
{
	const int32 kOldProcessWaitMs = 20000;

	class RelaunchWindow
	{
		public Placement placement;
		public Core::String root;   // the folder (or the one solution in it) its workspace opens from; "" for the start page
	}

	// {"pid": <the old process>, "windows": [{"root": ..., "placement": {...}}]}
	Core::String RelaunchJson(int64 pid, const Core::Array<RelaunchWindow>& windows);
	// False when `text` is not a relaunch file.
	bool ParseRelaunch(Core::StringView text, int64& pid, Core::Array<RelaunchWindow>& windows);
	// The workspace a window's address names (`?ws=`, the web client keeps it there); "" for none.
	Core::String WorkspaceOf(Core::StringView url);
	// The root of workspace `wsId` in the control channel's reply to `status` (Launch::Reply's line); "" when it is not
	// open.
	Core::String RootOf(Core::StringView statusReply, Core::StringView wsId);
	// Waits up to `ms` for process `pid` to end; true once it has (or never ran).
	bool AwaitExit(int64 pid, int32 ms);
}
