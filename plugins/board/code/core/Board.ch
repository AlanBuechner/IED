// A board in memory (docs/32): its layers, its elements in drawing order, the tile index over them, its plans and
// the history of its changes. Read from and written to a .iedboard file (one element per line).

include "Shapes.ch"

namespace Boards
{
	// The side of a tile of the board's grid, in world units (docs/32 "Coordinates and tiles").
	const double c_TileSize = 512.0;
	// The changes a board remembers for undo and revert.
	const int32 c_HistoryLimit = 500;

	class Layer
	{
		public Core::String m_Id;
		public Core::String m_Name;
		public bool m_Visible = true;
		public bool m_Locked = false;
		public double m_Opacity = 1.0;
	}

	// A plan kept in the board: Markdown or Mermaid text the owner and the AI iterate on.
	class Plan
	{
		public Core::String m_Id;
		public Core::String m_Title;
		public Core::String m_Kind;   // "markdown" or "mermaid"
		public Core::String m_Text;
		public Core::String m_By;
	}

	// What one change did to one element: its state before and after (null where it was or became absent) and its
	// place in the drawing order then.
	class ElementChange
	{
		public Core::String m_Id;
		public Element* m_Before;
		public Element* m_After;
		public int64 m_IndexBefore = -1;
		public int64 m_IndexAfter = -1;
		// What the element was when the change was last undone (null: absent), for a redo that keeps what others did.
		public Element* m_AtUndo;
		public bool m_Rewound;
	}

	class Change
	{
		public Core::String m_Id;
		public int64 m_Rev;
		public Core::String m_By;
		public Core::String m_Label;
		public int64 m_TimeMs;
		public Core::Array<ElementChange> m_Elements;
		public bool m_LayersChanged;
		public Core::Array<Layer> m_LayersBefore;
		public Core::Array<Layer> m_LayersAfter;
		public bool m_TitleChanged;
		public Core::String m_TitleBefore;
		public Core::String m_TitleAfter;
		public bool m_Undone;
	}

	// The tiles an element's bounds touch, inclusive.
	struct TileSpan
	{
		public int64 m_X0;
		public int64 m_Y0;
		public int64 m_X1;
		public int64 m_Y1;
	}

	class Board
	{
		[operator("create")] public void Init() {}

		public Element* Find(Core::StringView id) const;
		public int64 IndexOf(Core::StringView id) const;
		public Layer* FindLayer(Core::StringView id);
		public int64 LayerIndex(Core::StringView id) const;
		// A layer by id or, failing that, by name (any case).
		public Layer* LayerNamed(Core::StringView idOrName);
		public Core::String NewElementId();
		public Core::String NewLayerId();
		public Core::String NewPlanId();
		public Core::String NewChangeId();

		// A connector's route between the elements it binds now; empty for anything else or a dangling connector.
		public Core::Array<double> RouteOf(const Element& element) const;
		public Box Bounds(const Element& element) const;
		// Every element's bounds together; `visibleOnly` leaves out hidden layers.
		public Box ContentBounds(bool visibleOnly) const;

		// Puts a new element in the drawing order at `index` (-1: the end) and in the tile index.
		public void Insert(Element* element, int64 index);
		public void RemoveAt(int64 index);
		// Moves the element at `from` to `to` in the drawing order.
		public void Reorder(int64 from, int64 to);
		// Indexes the element again after its geometry changed, and the connectors bound to it.
		public void Touched(Core::StringView id);
		public void ReindexAll();
		// The elements whose bounds meet `region`, in drawing order (layers bottom to top).
		public Core::Array<Element*> Near(const Box& region) const;
		// Every element in drawing order: by layer, bottom to top, then by their order.
		public Core::Array<Element*> Drawn(bool visibleOnly) const;
		// The connectors bound to `id`.
		public Core::Array<Element*> ConnectorsOf(Core::StringView id) const;
		// The elements a frame holds: those on its own layer whose bounds' centers are inside its box.
		public Core::Array<Element*> MembersOf(const Element& frame) const;
		public bool LayerVisible(Core::StringView id) const;

		// Records a change in the history (dropping the oldest past the limit).
		public void Record(Change* change);
		public Change* FindChange(Core::StringView id) const;

		public Core::String m_Title;
		public Core::Array<Layer> m_Layers;
		public Core::Array<Element*> m_Elements;
		public Core::HashMap<Core::String, Element*> m_ById;
		public Core::HashMap<int64, Core::Array<Core::String>> m_Tiles;
		public Core::HashMap<Core::String, TileSpan> m_Spans;
		public Core::Array<Plan> m_Plans;
		public Core::Array<Change*> m_History;
		public int64 m_NextId = 1;
		public int64 m_NextLayer = 1;
		public int64 m_NextPlan = 1;
		public int64 m_NextChange = 1;
		public int64 m_Rev;

		private void Index(Element* element);
		private void Unindex(Core::StringView id);
	}

	// A copy of the board for trying a change on (its elements copied; its history left behind).
	Board* CloneBoard(const Board& board);
	// A new board: one layer, "Layer 1".
	void InitBoard(Board& board, Core::StringView title);
	// Reads a .iedboard file's text into an empty board; false with `problem`.
	bool ReadBoard(Core::StringView text, Board& board, Core::String& problem);
	// The board as a .iedboard file's text.
	Core::String WriteBoard(const Board& board);
	void WriteLayer(JsonOut& json, const Layer& layer);
	void WritePlan(JsonOut& json, const Plan& plan);
	// The tile of a world coordinate.
	int64 TileOf(double coordinate);
}
