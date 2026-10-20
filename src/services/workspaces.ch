// Workspaces: folders the server opened, each a "ws:<wsId>" topic, and the fs.* methods over them with revision
// checks (docs/10). A file's revision starts at 1 when the server first sees it and grows with every write it makes;
// a write that names the revision it was based on is refused with conflict when the file moved on (0: must not exist).

include "../web/rpc.ch"

namespace Ied
{
	class Workspace
	{
		public Core::String wsId;
		public Core::String name;
		public FS::Path root;
		public bool trusted;
		public Core::HashMap<Core::String, int64> revisions;   // relative path -> revision

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
		// A file's text; throws not_found.
		public Core::String Read(Workspace* ws, Core::StringView path) const;
		// The file's revision now (0 when it does not exist).
		public int64 Revision(Workspace* ws, Core::StringView path);
		// Writes a file (its folders made), checking `expectedRevision` unless it is -1; the new revision.
		public int64 Write(Workspace* ws, Core::StringView path, Core::StringView text, int64 expectedRevision);
		public Workspace* Open(const FS::Path& folder);

	private:
		private Core::String Snapshot(const Workspace* ws) const;
		private void TreeChanged(Workspace* ws, Core::StringView path, const char* change);
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
}
