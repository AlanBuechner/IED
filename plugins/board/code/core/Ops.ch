// Changing a board (docs/32 "Changes, undo and the server's ownership"): a list of operations applied as one change,
// recorded with every element's state before and after so it can be undone, redone or reverted alone.

include "Layout.ch"
include "Font.ch"

namespace Boards
{
	// What a change did, for its event and its answer.
	class ChangeResult
	{
		public Change* m_Change;                                   // null when it failed or changed nothing
		public Core::Array<Core::String> m_RefNames;              // the refs it named, in order
		public Core::HashMap<Core::String, Core::String> m_Refs;  // ref -> the new element's or layer's id
		public Core::Array<Core::String> m_Done;                  // a line per op
		public Core::Array<Core::String> m_Warnings;
		public Core::String m_Error;                              // refused: nothing applied
	}

	// Keeps the state of each element a change touches before it does, and after, as the change goes.
	class Recorder
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Board* board, Change* change);

		// Call before changing element `id` (one that exists, or a new id about to be added).
		public void Before(Core::StringView id);
		// Call before changing the layers or the title.
		public void BeforeLayers();
		public void BeforeTitle();
		// Records every touched element's state after the change; true when anything changed.
		public bool Finish();

		public Board* m_Board;
		public Change* m_Change;
		private Core::HashMap<Core::String, int64> m_StartIndex;
		private Core::HashMap<Core::String, bool> m_Seen;
	}

	// Applies `ops` (a JSON array of op objects) to the board as one change by `by` ("owner", "ai") labeled `label`:
	// all of it or, when an op is refused, none (m_Error says which and why).
	// `imageSize` reads an image file's size in pixels for an added image that does not give it (null: such an add is
	// refused).
	ChangeResult ApplyOps(Board& board, const Serialization::DomNode& ops, Core::StringView by, Core::StringView label, int64 nowMs, func<bool(Core::StringView, double&, double&, Core::String&)> imageSize);
	// Puts the board back as it was before `change` (undo) or as it was after it (redo); the elements, layers and
	// title it touched only. The board's revision moves.
	void Rewind(Board& board, Change* change, bool undo);
	// The change of `by` an undo takes back: its latest not undone. Null when none.
	Change* UndoTarget(const Board& board, Core::StringView by);
	// The change of `by` a redo repeats: its latest undone with no later change of its own after it. Null when none.
	Change* RedoTarget(const Board& board, Core::StringView by);
	// The size a node, note or text gets for its text when the op gives none.
	void AutoSize(Element& element);
}
