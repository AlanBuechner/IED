// Workspaces: folders the server opened, each a "ws:<wsId>" topic, and the fs.* methods over them with revision
// checks (docs/10). A file's revision starts at 1 when the server first sees it and grows with every write, the
// server's or anyone else's; a write that names the revision it was based on is refused with conflict when the file
// moved on (0: must not exist). Each folder is watched (fs/watching): a change made behind the server's back is
// published as tree.changed and reaches the documents open on it, while the server's own writes are recognized by the
// digest of what it wrote and not reported twice.

include "web/rpc.ch"
include "fs/watching.ch"
include "solutions/model.ch"
include "localstate.ch"

namespace Ied
{
	class WorkspaceServers;

	class Workspace
	{
		public Core::String wsId;
		public Core::String name;
		public FS::Path root;
		public Core::String kind;          // "folder", "file" (opened for one file) or "solution"
		public FS::Path localState;        // its per-machine state, what ".ied/local/..." paths name (localstate.ch)
		public SolutionModel* solution;    // a solution workspace's model; null otherwise
		public bool trusted;
		public bool byPlugin;              // opened by a plugin (IED::OpenFolder), not the owner: a relaunch leaves it closed
		public FS::Path target;            // the master's: what its server's --workspace names (a solution file, a folder, a file)
		public WorkspaceServers* servers;  // the master's: its summary carries its server's state
		public Core::HashMap<Core::String, int64> revisions;        // relative path -> revision
		public Core::HashMap<Core::String, Core::String> digests;   // relative path -> SHA-256 of the text the server last wrote or read; "" once it deleted the file
		public Core::HashMap<Core::String, bool> ownDirectories;    // folders the server made, until the watcher reports them
		public FolderWatch watch;

		// The file at a workspace-relative path; under .ied/local, in its local state.
		public FS::Path At(Core::StringView path) const;
		public Core::String SummaryJson() const;
	}

	class Workspaces
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The lock is held for all of these.
		// Throws not_found for an unknown id; Find answers null.
		public Workspace* Get(Core::StringView wsId) const;
		public Workspace* Find(Core::StringView wsId) const;
		// The open workspace rooted at `root`, or null.
		public Workspace* ForRoot(const FS::Path& root) const;
		// The services see no workspace in the master: its records are open in their own servers (docs/01 "Processes"), so
		// Get, Find, ForRoot, First and All answer as if none were open there; Listed finds the master's record.
		public Workspace* First() const { return items.IsEmpty() || master ? nullptr : items[0]; }
		public Workspace* Listed(Core::StringView wsId) const;
		// What opens each workspace the owner opened again - a solution's file, else its folder - in the order they
		// opened, so they get their ids back.
		public Core::Array<Core::String> OwnerPaths() const;
		public const Core::Array<Workspace*>& All() const;
		public Core::String SummariesJson() const;
		// A file's text, the file seen from now on (its revision and digest kept); throws not_found.
		public Core::String Read(Workspace* ws, Core::StringView path);
		// The file's revision now (0 when it does not exist).
		public int64 Revision(Workspace* ws, Core::StringView path);
		// Writes a file (its folders made), checking `expectedRevision` unless it is -1; the new revision.
		public int64 Write(Workspace* ws, Core::StringView path, Core::StringView text, int64 expectedRevision);
		// A folder as a workspace of `kind` ("folder", "file" or "solution"), told to every service.
		public Workspace* Open(const FS::Path& folder, Core::StringView kind);
		// The workspace `target` (a canonical path) belongs to, opening it when none does: a .iedsln as a solution, a
		// folder as its one solution file or else as itself, another file in its folder; `relative` is the path inside
		// it. It goes first in the recent projects (host/recent).
		public Workspace* OpenFor(const FS::Path& target, Core::String& relative);
		// A .iedsln (a canonical path) as a solution workspace rooted at its folder.
		public Workspace* OpenSolution(const FS::Path& file);
		// The master: a workspace's server closed it for good: it leaves the list (workspace.closed).
		public void Closed(Core::StringView wsId);
		// The master: what a workspace server says of its workspace now (master.workspace): its name, kind and trust, and
		// `solutionFile` when it became a solution; workspace.changed.
		public void ServerSays(Core::StringView wsId, const Serialization::DomNode& summary, Core::StringView solutionFile);
		// The master: a trust choice its workspace server made (master.trust), for the record of the workspace at `root`.
		public void TrustSaid(const FS::Path& root, bool trusted);
		// A workspace server: trust.json changed (read again): each workspace whose trust it changed is told.
		public void TrustFileChanged();
		// What the watcher saw, less what the server did itself: published and passed to the open documents.
		public void ApplyExternal(Workspace* ws, const ChangeBatch& batch);
		// Its project plugins changed: their code reconciles and then ContributionsChanged.
		public void PluginsChanged(Workspace* ws);
		// What its plugins contribute changed: its settings catalog and task offers are made again and its clients read
		// the contributions again (plugins.changed on ws:<wsId>).
		public void ContributionsChanged(Workspace* ws);
		// Its solution file or a project file changed: the model is read again now and solution.changed published, so every
		// client's Solution Explorer shows it without waiting for the watcher.
		public void SolutionChanged(Workspace* ws);
		// An open folder workspace whose root now holds the solution `file` becomes that solution in place (docs/03
		// "Managing projects"): same wsId, chats, terminals and local state for this run; solution.changed carries its
		// new kind and name, and workspace.changed on the server topic its summary.
		public void BecomeSolution(Workspace* ws, const FS::Path& file);
		// A folder of `ws` moved with what is in it: revisions and digests follow, and so do the documents open under
		// it; throws conflict when `to` is there, unavailable when something holds a file in it open.
		public void MoveFolder(Workspace* ws, Core::StringView from, Core::StringView to);
		// A folder of `ws` with everything in it to the trash; its trash entry's id.
		public Core::String TrashFolder(Workspace* ws, Core::StringView path);
		// A file of `ws` to the trash; its trash entry's id.
		public Core::String TrashFile(Workspace* ws, Core::StringView path);

	private:
		private Workspace* OpenOrFind(const FS::Path& target, Core::String& relative);
		private void Remember(Workspace* ws);
		// recent.changed on the server topic: the whole list.
		private void PublishRecent();
		private Core::String Snapshot(const Workspace* ws) const;
		private void TreeChanged(Workspace* ws, Core::StringView path, const char* change);
		// tree.changed for a move: `from` deleted, `to` created.
		private void Moved(Workspace* ws, Core::StringView from, Core::StringView to);
		private Core::String DeleteFolder(Workspace* ws, Core::StringView path, bool recursive);
		private Core::String RenameFolder(Workspace* ws, Core::StringView from, Core::StringView to);
		private void PublishSolution(Workspace* ws);
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
		private Core::String OpenRecentMethod(Call& call, const Params& params);
		// ws.setFavorite, or ws.forgetRecent when `forget`.
		private Core::String RecentChangeMethod(Call& call, const Params& params, bool forget);
		private void RegisterMethods();

		private Workspace* Record(const FS::Path& folder, Core::StringView kind);
		private bool Holds(const FS::Path& target, Core::String& relative) const;
		private Workspace* RootedAt(const FS::Path& root) const;
		private Workspace* OpenElsewhere(const FS::Path& target);
		// Its trust changed: every service told, trust.changed on ws:<wsId>.
		private void ApplyTrust(Workspace* ws, bool trusted);
		private Core::String NextId();
		private void Serve(Workspace* ws, const FS::Path& target);
		// A workspace server: what changed of its own workspace goes to the master too.
		private void TellMaster(Workspace* ws, Core::StringView solutionFile);

		private Server* server;
		private Core::Array<Workspace*> items;
		private Core::Array<Workspace*> none;
		private bool master;    // this is the master's list of records
		private int64 opened;   // ids handed out
	}

	// A workspace-relative path with '/' separators and no '.' or '..'; throws bad_request.
	Core::String NormalizePath(Core::StringView path);
	// The SHA-256 of a file's text, hex.
	Core::String TextDigest(Core::StringView text);
	// Paths no one browses: a repository's own folder.
	bool IsHiddenPath(Core::StringView path);
	// `path` relative to `root` with '/' separators, comparing without regard to ASCII case or separator kind (Windows
	// paths); false when it is not `root` or under it.
	bool RelativeTo(Core::StringView root, Core::StringView path, Core::String& relative);
}
