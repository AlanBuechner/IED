// What the owner's freehand strokes look like (docs/32 "Recognition"): strokes grouped into sketch groups, each a
// guess - a rectangle, ellipse or diamond drawn in one or more strokes, a frame (a big box drawn around shapes), a line
// or an arrow (and the shapes or nodes its ends touch), handwriting (a shape's label inside it, a frame's title in its top
// band, an arrow's label beside its middle) or a scribble - with how well it fits.

include "Board.ch"

namespace Boards
{
	enum class Guess { Rectangle, Ellipse, Diamond, Line, Arrow, Handwriting, Scribble, Frame }

	Core::StringView GuessName(Guess guess);

	class SketchGroup
	{
		public Core::String m_Name;                 // g1, g2, ... in reading order
		public Core::Array<Core::String> m_Ids;     // its strokes
		public Guess m_Guess;
		public double m_Fit;                        // 0..1: how sure the guess is
		public Box m_Box;
		public Core::String m_Color;                // its strokes' color
		// A line or arrow: its ends (world), whether the head is at the end, at the start or both, and what each end
		// touches - a group's name or an element's id ("" for nothing).
		public Core::Array<double> m_Line;
		public bool m_HeadAtEnd;
		public bool m_HeadAtStart;
		public Core::String m_From;
		public Core::String m_To;
		// Handwriting: the shape group or node it sits in ("" for none); a shape: the handwriting groups inside it.
		public Core::String m_Inside;
		public Core::Array<Core::String> m_Holds;
	}

	// The sketch groups of the board's strokes - those among `ids` when it is not empty, else every stroke on a visible
	// layer - named in reading order. The same board always gives the same groups.
	Core::Array<SketchGroup> AnalyzeSketch(const Board& board, const Core::Array<Core::String>& ids);
	// The first and last points of a group's longest stroke (a misread line's ends).
	Core::Array<double> LongestStrokeEnds(const Board& board, const SketchGroup& group);
	// What the board has at a line's end (x, y): a shape group's name or a node's id, "" for nothing near.
	Core::String EndOf(const Board& board, const Core::Array<SketchGroup>& groups, double x, double y, double length);
	// Whether a guess becomes a node.
	bool IsShapeGuess(Guess guess);
}
