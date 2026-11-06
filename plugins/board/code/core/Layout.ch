// Placing elements so the AI never has to compute coordinates (docs/32 "AI tools"): a layered layout of nodes and the
// connectors among them, alignment and even spacing. Each answers how far to move each element (id -> dx, dy).

include "Board.ch"

namespace Boards
{
	const double c_LayoutGap = 70.0;

	// Lays out the boxed elements among `ids` (frames left out) in ranks along `direction` - TD (top down), LR, BT, RL
	// - by the connectors between them: ranks by longest path from the sources, order in a rank kept from where they
	// stand and then pulled toward their neighbours. Nodes `clusters` puts in one frame (node id -> frame id) stay
	// together and others are kept out of their frame's span. The layout keeps its top left where the elements' was.
	Core::HashMap<Core::String, Point> LayeredLayout(const Board& board, const Core::Array<Core::String>& ids, Core::StringView direction, double gap, const Core::HashMap<Core::String, Core::String>& clusters);
	// The layout of `ids` and `keep` together, moved so the kept nodes sit where they stand, and only the others placed:
	// each pushed clear of what is on the board. Moves for the others alone.
	Core::HashMap<Core::String, Point> StableLayout(const Board& board, const Core::Array<Core::String>& ids, const Core::Array<Core::String>& keep, Core::StringView direction, double gap, const Core::HashMap<Core::String, Core::String>& clusters);
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
