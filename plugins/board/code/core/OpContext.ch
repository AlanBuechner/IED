// The state one change keeps while its ops run (Ops.cf, LayerOps.cf): the board, the recorder, the answer being
// built, and where elements the ops gave no place go.

include "Ops.ch"

namespace Boards
{
	class OpContext
	{
		public Board* m_Board;
		public Recorder m_Recorder;
		public ChangeResult* m_Result;
		public Core::String m_By;
		public Core::String m_Problem;
		public bool m_Placing;
		public double m_PlaceX;
		public double m_PlaceY;
		public double m_PlaceTop;
		public double m_PlaceColumn;
		public int32 m_Placed;
		// An image file's size in pixels (the service reads it); null: images by path cannot be added.
		public func<bool(Core::StringView, double&, double&, Core::String&)> m_ImageSize;

		public bool Fail(Core::String problem) {
			m_Problem = problem;
			return false;
		}
		public void Done(Core::String line) { m_Result->m_Done.Push(line); }
		public void Warn(Core::String line) { m_Result->m_Warnings.Push(line); }
	}

	// An element id an op names: an existing id, "@ref" or a bare ref of this change. False with the context's
	// problem when it names nothing.
	bool ResolveId(OpContext& ctx, Core::StringView raw, Core::String& id);
	// The ids an op's `key` names: a string or an array of strings, each resolved.
	bool ResolveIds(OpContext& ctx, const Serialization::DomNode& op, const char* key, Core::Array<Core::String>& ids);
	// A layer by id, name or "@ref"; null with the context's problem.
	Layer* ResolveLayer(OpContext& ctx, Core::StringView raw);
	// "120,80": a box's top left.
	Core::String Where(const Element& element);
	// "e12 rect "API" at 100,80 160x60" - what an answer says of an element.
	Core::String Brief(const Board& board, const Element& element);
	// Moves an element (not a connector: it follows its ends) by dx, dy.
	void Offset(Element& element, double dx, double dy);
	// Moves each element by its delta, recorded. With `refit` (a layout, a tidy) frames whose members moved are fitted
	// around them again; without, a member moved out of its frame leaves it.
	void ApplyMoves(OpContext& ctx, const Core::HashMap<Core::String, Point>& moves, bool refit);

	bool AddLayerOp(OpContext& ctx, const Serialization::DomNode& op);
	bool UpdateLayerOp(OpContext& ctx, const Serialization::DomNode& op);
	bool DeleteLayerOp(OpContext& ctx, const Serialization::DomNode& op);
	bool MergeLayerOp(OpContext& ctx, const Serialization::DomNode& op);
	bool TitleOp(OpContext& ctx, const Serialization::DomNode& op);
	bool LayoutOp(OpContext& ctx, const Serialization::DomNode& op);
	bool AlignOp(OpContext& ctx, const Serialization::DomNode& op);
	bool DistributeOp(OpContext& ctx, const Serialization::DomNode& op);
	bool PlaceOp(OpContext& ctx, const Serialization::DomNode& op);
	bool TidyOp(OpContext& ctx, const Serialization::DomNode& op);
}
