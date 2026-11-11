// The ied.board plugin's server side (docs/32): the boards open in each workspace, kept in memory, changed by the
// owner's clients and the AI through one path, saved a moment after each change and read again when their file
// changes on disk.
//
// Topics: ied.board/ws:<wsId> - {boards} the workspace's .iedboard files; event "boards.changed" (the new list).
// ied.board/b:<boardId> - {boardId, path, rev, title, layers, elements, plans, history}; events "changed" (a change,
// an undo, a redo or a revert: {rev, change, put: [{at, el}], removed, layers?, title?, refs?, opId?}), "plans"
// ({plans}), "reloaded" (the whole board again after its file changed on disk).
//
// Locks: a board's state changes only while its own lock is held and inside PluginContext::Atomically, so the
// topic's pulled snapshot (taken under the server's lock) never sees half a change, and readers (pictures, text,
// exports) need only the board's lock. The service's lock guards the list of open boards and is never held while a
// board's is taken.

include "core/Describe.ch"
include "core/History.ch"
include "core/Mermaid.ch"
include "core/Render.ch"
include "api/Plugin.ch"

namespace Boards
{
	const char* c_Extension = ".iedboard";
	const int64 c_SaveDelayNs = 800000000;

	class OpenBoard
	{
		public Core::String m_Id;          // "<wsId>:<16 hex digits of its path's hash>"
		public Core::String m_WsId;
		public Core::String m_Path;        // workspace-relative
		public Board* m_Board;
		public Threading::Mutex m_Lock;
		public bool m_Dirty;
		public int64 m_ChangedNs;
		public Core::String m_Saved;       // the file's text as last read or written

		public Core::String Topic() const { return Core::String("ied.board/b:") + m_Id.View(); }
	}

	// A board's per-machine file: ".ied/local/boards/<16 hex digits of its path's hash><suffix>" - ".json" its local
	// state (the chat it talks to, the camera), ".history.json" its history (core/History.ch).
	Core::String LocalFile(Core::StringView boardPath, Core::StringView suffix);

	// What a tool answers: its text and, for board_view, the picture's workspace path.
	class ToolAnswer
	{
		public Core::String m_Text;
		public Core::String m_Image;
	}

	class BoardService
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void Start();
		public void Stop();
		public void Opened(const IED::Workspace& ws);
		public void FilesChanged(const IED::FileChanges& changes);

		// The board at a workspace path, loaded when it is not open; throws not_found, bad_request.
		public OpenBoard* Open(Core::StringView wsId, Core::StringView path);
		// A new board file; throws conflict when one is there.
		public OpenBoard* Create(Core::StringView wsId, Core::StringView path, Core::StringView title);
		public OpenBoard* ById(Core::StringView boardId);
		// The workspace's .iedboard files.
		public Core::Array<Core::String> BoardFiles(const IED::Workspace& ws);
		public Core::String ListJson(const IED::Workspace& ws);

		// Applies ops as one change by `by`, publishes it; the result as the edit method answers.
		public Core::String Edit(OpenBoard* open, const Serialization::DomNode& ops, Core::StringView by, Core::StringView label, Core::StringView opId, ChangeResult& result);
		// Undo or redo of `by`'s latest; a revert of one change by id when `changeId` is given.
		public Core::String Rewound(OpenBoard* open, Core::StringView by, bool undo, Core::StringView changeId);
		public Core::String SetPlan(OpenBoard* open, const Serialization::DomNode& args, Core::StringView by);

		public Core::String SnapshotJson(OpenBoard* open);
		public Theme LoadTheme();
		// The palette's names to the theme's colors as #rrggbb, for Mermaid's classDef lines.
		public Core::HashMap<Core::String, Core::String> HueHex();
		// An image file of the workspace read for a picture (PNG); false when it cannot be.
		public bool LoadImage(const IED::Workspace& ws, Core::StringView path, Imaging::Rgba& image);
		// An image file's size in pixels (PNG or JPEG).
		public bool ImageSize(const IED::Workspace& ws, Core::StringView path, double& w, double& h, Core::String& problem);
		// Keeps an uploaded image beside the board: <board>.assets/<digest>.<ext>; its path and size.
		public Core::String StoreImage(OpenBoard* open, Core::StringView blobId, double& w, double& h);

		// A board tool's answer, its changes by `by` ("ai", "owner").
		public ToolAnswer RunTool(const IED::Workspace& ws, Core::StringView tool, const Serialization::DomNode& args, Core::StringView by);

		public IED::PluginContext* m_Context;
		public Threading::Mutex m_Lock;
		public Core::HashMap<Core::String, OpenBoard*> m_Boards;
		public bool m_Stopping;
		public int64 m_ViewCount;

		// Saves the boards whose last change is old enough; a board now.
		public void SaveDue();
		public void Save(OpenBoard* open);
		public void PublishList(Core::StringView wsId);
	}

	Core::String BoardIdFor(Core::StringView wsId, Core::StringView path);
	// `path` normalized, with the .iedboard extension added when it has none.
	Core::String BoardPath(Core::StringView path);
	// A change's summary as the events and the history carry it.
	void WriteChange(JsonOut& json, const Change& change);
	// The elements a change touched as they are now ({at, el}) and those it left absent.
	void WriteTouched(JsonOut& json, const Board& board, const Change& change);
	void RegisterMethods(BoardService* service);
	// Board groups (Groups.cf, docs/32 "Groups"): the list's groups of the boards in `existing`; whether `path` is the
	// workspace's groups file; ied.board/groups.
	void WriteGroups(JsonOut& json, const IED::Workspace& ws, const Core::Array<Core::String>& existing);
	bool IsGroupsFile(Core::StringView path);
	Core::String SetGroups(BoardService* service, const IED::Workspace& ws, const Serialization::DomNode& groups);
	void ProvideQuery(BoardService* service);
	// The tool's reply text for a change: what it did, its warnings, its refs.
	Core::String ChangeText(const Board& board, const ChangeResult& result);
	int64 NowMs();
}
