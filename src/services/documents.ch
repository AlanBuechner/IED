// Documents (docs/10 document editing): server-owned buffers, each a "doc:<docId>" topic. The first opener holds the
// lease and only the holder edits; an edit made against an older version is rebased over the edits since, or refused
// with conflict when it overlaps one. Server-side writers (a reload from disk, an AI tool) edit as the server, with
// their own origin. Every method here runs with the server lock held.

include "fs/textedit.ch"
include "workspaces.ch"

namespace Ied
{
	const int64 kDocumentHistoryVersions = 1000;

	// The changes that made one version.
	class VersionChanges
	{
		public int64 version;
		public Core::Array<Change> changes;
	}

	class Document
	{
		public Core::String docId;
		public Workspace* ws;
		public Core::String path;
		public Core::String text;
		public int64 version;
		public Core::String holder;   // the client that holds the lease; empty for none
		public bool dirty;            // the text differs from `saved`
		public Core::String saved;    // the file's text as last read, saved or seen changed on disk
		public Core::String opened;   // the text when the server opened it: the track-changes margin's baseline
		public Core::Queue<VersionChanges*> history;

		public Core::String Topic() const { return Core::String("doc:") + docId.View(); }
	}

	class Documents
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public Document* Get(Core::StringView docId) const;
		// The open document of a file, or null.
		public Document* Find(Workspace* ws, Core::StringView path) const;
		// Applies an edit made against `baseVersion` as `origin`, rebased when the document moved on; the edit's result.
		public Core::String ApplyEdit(Document* doc, int64 baseVersion, const Core::Array<Change>& changes, Core::StringView origin, Core::StringView opId);
		// A file written behind an open document: a clean one reloads, a dirty one is told.
		public void OnDiskWrite(Workspace* ws, Core::StringView path, Core::StringView text, int64 revision);
		// A file deleted behind an open document: it is told (doc.diskChanged with revision 0) and keeps its text.
		public void OnDiskDeleted(Workspace* ws, Core::StringView path);
		// A file moved by the server (fs.rename): its open document follows, and is told (doc.renamed {docId, path}).
		public void Renamed(Workspace* ws, Core::StringView from, Core::StringView to);
		public bool IsOpen(Workspace* ws, Core::StringView path) const;
		public Core::Array<Core::String> OpenPaths(Workspace* ws) const;

	private:
		private Document* Open(Workspace* ws, Core::StringView path);
		private Core::String Snapshot(const Document* doc) const;
		private Core::Array<Change> Rebase(const Document* doc, int64 baseVersion, const Core::Array<Change>& changes) const;
		private void Grant(Document* doc, Core::StringView clientId);
		private Core::String OpenMethod(Call& call, const Params& params);
		private Core::String EditMethod(Call& call, const Params& params);
		private Core::String TakeLeaseMethod(Call& call, const Params& params);
		private Core::String SaveMethod(Call& call, const Params& params);
		private Core::String BaselineMethod(Call& call, const Params& params);

		private Server* server;
		private Core::HashMap<Core::String, Document*> byId;
		private Core::HashMap<Core::String, Document*> byPath;   // "<wsId>/<path>"
		private int64 opened;
	}

	// The changes of an edit request; throws bad_request.
	Core::Array<Change> ChangesParam(const Params& params);
}
