// Documents (docs/10 document editing): server-owned buffers, each a "doc:<docId>" topic. The first opener holds the
// lease and only the holder edits; an edit made against an older version is rebased over the edits since, or refused
// with conflict when it overlaps one. Server-side writers (a reload from disk, an AI tool) edit as the server, with
// their own origin. Every method here runs with the server lock held.

include "fs/TextEdit.ch"
include "Workspaces.ch"

namespace Ied
{
	const int64 c_DocumentHistoryVersions = 1000;

	// The changes that made one version.
	class VersionChanges
	{
		public int64 m_Version;
		public Core::Array<Change> m_Changes;
	}

	class Document
	{
		public Core::String m_DocId;
		public Workspace* m_Ws;
		public Core::String m_Path;
		public Core::String m_Text;
		public int64 m_Version;
		public Core::String m_Holder;   // the client that holds the lease; empty for none
		public bool m_Dirty;            // the text differs from `saved`
		public Core::String m_Saved;    // the file's text as last read, saved or seen changed on disk
		public Core::String m_Opened;   // the text when the server opened it: the track-changes margin's baseline
		public bool m_External;         // a file outside the workspace, read-only; `path` is its file: URI (docs/10)
		public Core::String m_ReadOnlyReason;   // why a file of a read-only folder is read-only (ReadOnly.ch); empty for none
		public Core::String m_LanguageId;   // the language its clients were last told
		public Core::Queue<VersionChanges*> m_History;

		public Core::String Topic() const { return Core::String("doc:") + m_DocId.View(); }
		public bool IsReadOnly() const { return m_External || !m_ReadOnlyReason.IsEmpty(); }
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
		// The plugins loaded for `ws` changed: each open document whose language is another now is told
		// (doc.language {docId, languageId}).
		public void LanguagesChanged(Workspace* ws);
		public bool IsOpen(Workspace* ws, Core::StringView path) const;
		// Writes the document's text to its file: doc.saved, and its language servers' didSave. The file's revision.
		public int64 Save(Document* doc);
		// Saves every dirty document of `ws` (a task's saveBefore, docs/06); how many.
		public int64 SaveAll(Workspace* ws);
		// The read-only folders of `ws` changed: each open document whose state changed is told (doc.readOnly {docId,
		// readOnly, reason}), and one that became read-only loses its lease.
		public void ReadOnlyChanged(Workspace* ws);
		// Throws forbidden (reason read_only) for a document that is read-only.
		public void RefuseReadOnly(const Document* doc) const;
		public Core::Array<Core::String> OpenPaths(Workspace* ws) const;

	private:
		private Document* Open(Workspace* ws, Core::StringView path);
		// A file outside the workspace that one of its language servers named; throws forbidden for another.
		private Document* OpenExternal(Workspace* ws, Core::StringView uri);
		private Document* Added(Workspace* ws, Core::StringView key, Core::StringView path, Core::String text, bool external);
		private Core::String Snapshot(const Document* doc) const;
		private Core::Array<Change> Rebase(const Document* doc, int64 baseVersion, const Core::Array<Change>& changes) const;
		private void Grant(Document* doc, Core::StringView clientId);
		private Core::String OpenMethod(Call& call, const Params& params);
		private Core::String EditMethod(Call& call, const Params& params);
		private Core::String TakeLeaseMethod(Call& call, const Params& params);
		private Core::String SaveMethod(Call& call, const Params& params);
		private Core::String BaselineMethod(Call& call, const Params& params);

		private Server* m_Server;
		private Core::HashMap<Core::String, Document*> m_ById;
		private Core::HashMap<Core::String, Document*> m_ByPath;   // "<wsId>/<path>"
		private int64 m_Opened;
	}

	// The changes of an edit request; throws bad_request.
	Core::Array<Change> ChangesParam(const Params& params);
}
