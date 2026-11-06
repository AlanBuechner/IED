// The board in words for the AI (docs/32 "AI tools"): what board_read answers and what board_recognize proposes.

include "Recognize.ch"
include "Diagram.ch"

namespace Boards
{
	class ReadAsk
	{
		public Box m_Region;                       // empty: all of it
		public Core::String m_Layer;               // a layer id; "": every layer
		public Core::Array<Core::String> m_Ids;    // just these
		public int32 m_Most = 400;                 // elements listed at most
	}

	// board_read's text: the layers, the diagram (nodes, connectors, frames), notes and text, images, free lines, the
	// sketch's groups with their guesses, and the plans. `path` names the board.
	Core::String DescribeBoard(const Board& board, Core::StringView path, const ReadAsk& ask, const Core::Array<SketchGroup>& groups);
	// One sketch group as a line: "g3 arrow (fit 0.82) at 300,140 190x26: from g1 to g4; strokes e6 e7".
	Core::String GroupLine(const SketchGroup& group);
	// Element ids as runs: "e3 e5-e9 e12".
	void AppendIdRuns(Core::StringBuilder& out, const Core::Array<Core::String>& ids);

	// What board_recognize is asked: the labels read off the picture (by group), types that override a guess (a group
	// to an element type), ends that override where a line runs ([from, to]: groups or element ids), the layer the clean
	// elements go on, how far right of the sketch they go, and whether the text is one line a group.
	class ProposalAsk
	{
		public Core::HashMap<Core::String, Core::String> m_Labels;
		public Core::HashMap<Core::String, Core::String> m_Types;
		public Core::HashMap<Core::String, Core::Array<Core::String>> m_Ends;
		public Core::String m_Layer;
		public double m_Dx;
		public bool m_Brief;
	}

	// What board_recognize proposes for the groups: the op that would replace each, as text for the model and as one
	// ops array. Handwriting a shape, frame or arrow holds becomes its label; scribbles are left.
	void Proposals(const Board& board, const Core::Array<SketchGroup>& groups, const ProposalAsk& ask, JsonOut& ops, Core::StringBuilder& text);
	// The groups a list of names or stroke ids picks ("g2", "e14"); every group when it is empty.
	Core::Array<SketchGroup> Picked(const Core::Array<SketchGroup>& groups, const Core::Array<Core::String>& names);
}
