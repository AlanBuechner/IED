// Placing elements so the AI never has to compute coordinates (docs/32 "AI tools"): a layered layout of nodes and the
// connectors among them, alignment and even spacing. Each answers how far to move each element (id -> dx, dy).

include "Board.ch"

namespace Boards
{
	const double c_LayoutGap = 70.0;
	// A frame's room around its members: each side and the bottom, and the title band on top.
	const double c_FramePad = 24.0;
	const double c_FrameTitle = 36.0;

	// The frames a layout keeps together.
	struct LayoutClusters
	{
	public:
		public Core::HashMap<Core::String, Core::String> m_Of;       // node id -> the innermost frame it is in
		public Core::HashMap<Core::String, Core::String> m_Parent;   // frame -> the frame it is in, when it is in one
	}

	// Lays out the boxed elements among `ids` (frames left out) in ranks along `direction` - TD (top down), LR, BT, RL
	// - by the connectors between them: ranks by longest path from the sources, order in a rank kept from where they
	// stand and then pulled toward their neighbours. A frame's members (`clusters`) are laid out on their own, inside it
	// first, and the frame then placed as one block among the rest, with room for its title and padding: frames never
	// overlap and hold only their members. The layout keeps its top left where the elements' was.
	Core::HashMap<Core::String, Point> LayeredLayout(const Board& board, const Core::Array<Core::String>& ids, Core::StringView direction, double gap, const LayoutClusters& clusters);
	// The layout of `ids` and `keep` together, moved so the kept nodes sit where they stand, and only the others placed:
	// each pushed clear of what is on the board. Moves for the others alone.
	Core::HashMap<Core::String, Point> StableLayout(const Board& board, const Core::Array<Core::String>& ids, const Core::Array<Core::String>& keep, Core::StringView direction, double gap, const LayoutClusters& clusters);
	// The arrangement kept, cleaned: nodes nearly in a row or a column lined up exactly, then snapped to `grid`.
	Core::HashMap<Core::String, Point> Tidied(const Board& board, const Core::Array<Core::String>& ids, double grid);
	// Lines the elements' `edge` up - left, right, top, bottom, centerx, centery - with that edge of `to`'s box when
	// given, else of the selection's bounds (left: the leftmost left edge).
	Core::HashMap<Core::String, Point> Aligned(const Board& board, const Core::Array<Core::String>& ids, Core::StringView edge, Core::StringView to);
	// Spaces the elements between the first and the last along `axis` (x or y) with equal gaps between them.
	Core::HashMap<Core::String, Point> Distributed(const Board& board, const Core::Array<Core::String>& ids, Core::StringView axis);
	// Whether `direction` is one LayeredLayout takes.
	bool IsDirection(Core::StringView direction);
}
