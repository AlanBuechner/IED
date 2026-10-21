// RecentProjects (docs/10 "Workspaces"): the folders and solutions the owner opened, newest first, and the ones they
// made favorites, kept in <data>/recent.json - machine-local, since the paths are this machine's. IED's start page and
// the workspace switcher list them, and a client may open again only what is listed here (ws.openRecent): a new path
// still comes from the machine itself (`ied <path>`, the desktop app's folder picker).

include "bedrock.ch"

namespace Ied
{
	const int64 kRecentLimit = 20;   // the projects kept besides the favorites

	struct RecentProject
	{
		public Core::String path;   // what opens it again: a folder, or a solution's .ied.sln
		public Core::String name;
		public Core::String kind;   // "folder" or "solution"
		public int64 opened;        // unix ms
		public bool favorite;
	}

	class RecentProjects
	{
		[operator("create")] public void Init() {}

		// The list in `file`; a missing file is an empty list, an unreadable one too (and says so in `error`).
		public void Load(Core::StringView file, Core::String& error);
		// `path` was opened at `now`: first in the list, added or moved up; past kRecentLimit, the oldest that are not
		// favorites drop off. False when the file could not be written.
		public bool Opened(Core::StringView path, Core::StringView name, Core::StringView kind, int64 now);
		public bool Contains(Core::StringView path) const;
		// For a listed path; false when the file could not be written.
		public bool SetFavorite(Core::StringView path, bool favorite);
		public bool Forget(Core::StringView path);
		public const Core::Array<RecentProject>& All() const { return items; }
		// [{path, name, kind, opened, favorite, exists}], newest first; `exists` is whether the path is still there.
		public Core::String Json() const;

		private int64 IndexOf(Core::StringView path) const;
		private bool Save();

		private Core::String file;
		private Core::Array<RecentProject> items;   // newest first
	}
}
