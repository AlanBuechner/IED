// KnownWorkspaces (docs/10 "Workspaces"): what the master keeps of its workspaces from one run to the next, in
// <data>/workspaces.json - machine-local, as the recent projects are.
//
// - Each folder that has been a workspace keeps its id (w1, w2, ...): it gets the same one whenever it opens again - after
//   a relaunch, `ied stop`, a crash or a reboot, whatever order the windows ask in - so an address's `?ws=`, a device's
//   stored choices and a layout's tab refs (`prof:<wsId>:...`) keep naming that folder, and an id never comes to name
//   another folder. Before 2026-11-15 ids were handed out in the order workspaces opened, and the desktop app opens its
//   windows' folders at once, so a relaunch could swap two projects' ids under their open pages.
// - The workspaces the owner has open (a solution by its .iedsln, any other by its folder, in the order they opened),
//   which a master opens again when it starts (`workspaces.reopen`), windows on them or not.

include "bedrock.ch"

namespace Ied
{
	const int64 c_KnownIdLimit = 500;   // the folders whose ids are kept; past it the longest unused goes (its id is not reused)

	struct KnownId
	{
		public Core::String m_Root;   // the workspace's folder, as it was opened
		public Core::String m_WsId;
		public int64 m_Used;          // unix ms it last opened
	}

	class KnownWorkspaces
	{
		[operator("create")] public void Init() {}

		// What `file` keeps; a missing file keeps nothing, an unreadable one nothing either (and says so in `error`).
		public void Load(Core::StringView file, Core::String& error);
		// The id of the workspace rooted at `root` opening at `now`: the one it had, unless one of `taken` (the ids open now)
		// has it, else the next number no folder was given. Kept (in the file) at once.
		public Core::String IdFor(Core::StringView root, int64 now, const Core::Array<Core::String>& taken);
		// The workspaces open now: what opens each again, in the order they opened. False when the file could not be
		// written.
		public bool SetOpen(const Core::Array<Core::String>& paths);
		public const Core::Array<Core::String>& Open() const { return m_Open; }
		public const Core::Array<KnownId>& Ids() const { return m_Ids; }

		private int64 IndexOf(Core::StringView root) const;
		private bool Save();

		private Core::String m_File;
		private Core::Array<KnownId> m_Ids;
		private Core::Array<Core::String> m_Open;
		private int64 m_Next;   // the number of the next new id: 1 more than any given, kept so an id that went is not reused
	}

	// The number in an id "w<n>", or 0 for anything else.
	int64 WorkspaceIdNumber(Core::StringView wsId);
}
