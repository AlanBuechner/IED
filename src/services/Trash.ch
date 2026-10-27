// Trash (docs/10 "Files", F-OPS-13): what fs.delete removes moves to <data>/trash/<id>/ - the file or folder under
// content/, and entry.json saying where it was - so trash.restore can put it back; trash.purge removes entries for
// good, and the oldest go when the trash holds more than kTrashCapBytes. Every member runs with the server lock held.

include "Workspaces.ch"

namespace Ied
{
	const int64 c_TrashCapBytes = 4294967296;

	class TrashEntry
	{
		public Core::String m_Id;
		public Core::String m_Root;     // the workspace folder it came from
		public Core::String m_Path;     // workspace-relative, '/' separators
		public Core::String m_Kind;     // "file" or "folder"
		public int64 m_DeletedMs;
		public int64 m_Bytes;

		// {id, root, path, kind, deletedMs, bytes}
		public Core::String ToJson() const;
	}

	class Trash
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// Moves `path` of `ws` (a folder when `folder`) into the trash; its entry's id. Throws unavailable when it
		// cannot be moved, leaving it where it was.
		public Core::String MoveIn(Workspace* ws, Core::StringView path, bool folder);

	private:
		private FS::Path Folder() const;
		// Every entry, newest first.
		private Core::Array<TrashEntry*> Entries() const;
		private TrashEntry* Read(Core::StringView id) const;
		private void Trim();
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String RestoreMethod(Call& call, const Params& params);
		private Core::String PurgeMethod(Call& call, const Params& params);

		private Server* m_Server;
		private int64 m_Made;
	}

	// Whether `id` has the form of a trash entry's id (digits and '-'), so it names a folder inside the trash.
	bool IsTrashId(Core::StringView id);
	// The bytes of a file, or of everything in a folder.
	int64 SizeOnDisk(const FS::Path& path);
	// Moves a file or folder, copying it across volumes when a rename cannot; false when it stayed where it was.
	bool MoveTree(const FS::Path& from, const FS::Path& to, bool folder);
}
