// Workspaces: folders the server opened, each a "ws:<wsId>" topic, and the fs.* methods over them with revision
// checks (docs/10). A file's revision starts at 1 when the server first sees it and grows with every write, the
// server's or anyone else's; a write that names the revision it was based on is refused with conflict when the file
// moved on (0: must not exist). Each folder is watched (fs/watching): a change made behind the server's back is
// published as tree.changed and reaches the documents open on it, while the server's own writes are recognized by the
// digest of what it wrote and not reported twice.

include "../web/rpc.ch"
include "../fs/watching.ch"

namespace Ied
{
	class Workspace
	{
		public Core::String wsId;
		public Core::String name;
		public FS::Path root;
		public bool trusted;
		public Core::HashMap<Core::String, int64> revisions;        // relative path -> revision
		public Core::HashMap<Core::String, Core::String> digests;   // relative path -> SHA-256 of the text the server last wrote or read; "" once it deleted the file
		public Core::HashMap<Core::String, bool> ownDirectories;    // folders the server made, until the watcher reports them
		public FolderWatch watch;

		// The file at a workspace-relative path.
		public FS::Path At(Core::StringView path) const;
		public Core::String SummaryJson() const;
	}

	class Workspaces
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The lock is held for all of these.
		public Workspace* Get(Core::StringView wsId) const;
		public Workspace* First() const { return items.IsEmpty() ? nullptr : items[0]; }
		public Core::String SummariesJson() const;
		// A file's text, the file seen from now on (its revision and digest kept); throws not_found.
		public Core::String Read(Workspace* ws, Core::StringView path);
		// The file's revision now (0 when it does not exist).
		public int64 Revision(Workspace* ws, Core::StringView path);
		// Writes a file (its folders made), checking `expectedRevision` unless it is -1; the new revision.
		public int64 Write(Workspace* ws, Core::StringView path, Core::StringView text, int64 expectedRevision);
		public Workspace* Open(const FS::Path& folder);
		// What the watcher saw, less what the server did itself: published and passed to the open documents.
		public void ApplyExternal(Workspace* ws, const ChangeBatch& batch);

	private:
		private Core::String Snapshot(const Workspace* ws) const;
		private void TreeChanged(Workspace* ws, Core::StringView path, const char* change);
		private void PublishTree(Workspace* ws, const Core::Array<TreeChange>& changes, bool overflow);
		private bool External(Workspace* ws, const TreeChange& change);
		private bool ExternalWrite(Workspace* ws, Core::StringView path);
		private bool ExternalDelete(Workspace* ws, Core::StringView path);
		private void Watch(Workspace* ws);
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String ReadMethod(Call& call, const Params& params);
		private Core::String WriteMethod(Call& call, const Params& params);
		private Core::String DeleteMethod(Call& call, const Params& params);
		private Core::String MakeDirectoryMethod(Call& call, const Params& params);
		private Core::String RenameMethod(Call& call, const Params& params);
		private Core::String SetTrustMethod(Call& call, const Params& params);
		private void RegisterMethods();

		private Server* server;
		private Core::Array<Workspace*> items;
	}

	// A workspace-relative path with '/' separators and no '.' or '..'; throws bad_request.
	Core::String NormalizePath(Core::StringView path);
	// The SHA-256 of a file's text, hex.
	Core::String TextDigest(Core::StringView text);
	// Paths no one browses: a repository's own folder.
	bool IsHiddenPath(Core::StringView path);
}
