// Reading a Mermaid flowchart and turning it into board ops (docs/32 "Mermaid"): nodes in Mermaid's shapes, edges in
// its link forms, subgraphs; merged into the board by key, then by label, the owner's positions kept and new nodes laid
// out beside them.

include "Diagram.ch"
include "Ops.ch"

namespace Boards
{
	class MermaidNode
	{
		public Core::String m_Key;
		public Core::String m_Label;
		public Kind m_Kind = Kind::Rect;
		public Core::String m_Subgraph;
		public bool m_Shaped;          // a statement gave its shape or label (a bare reference does not)
	}

	class MermaidEdge
	{
		public Core::String m_From;
		public Core::String m_To;
		public Core::String m_Label;
		public bool m_Dashed;
		public bool m_Thick;
		public bool m_End = true;
		public bool m_Start;
	}

	class MermaidSubgraph
	{
		public Core::String m_Key;
		public Core::String m_Title;
		public Core::String m_Parent;
	}

	class MermaidChart
	{
		public Core::String m_Direction = Core::String("TD");
		public Core::Array<MermaidNode> m_Nodes;
		public Core::Array<MermaidEdge> m_Edges;
		public Core::Array<MermaidSubgraph> m_Subgraphs;
		public Core::Array<Core::String> m_Notes;      // what was left out, and why
		public Core::HashMap<Core::String, Core::String> m_Colors;   // node key -> palette name, from class lines
		public Core::HashMap<Core::String, Core::String> m_Fills;
		public Core::HashMap<Core::String, Core::String> m_Descs;     // node key -> desc, from %% desc lines
		public Core::Array<Core::String> m_NoteTexts;                 // %% note lines: their text
		public Core::Array<Core::String> m_NoteAbout;                 // and the node key each is by ("" for none)

		public MermaidNode* Find(Core::StringView key);
	}

	// Parses a flowchart (or graph); false with `problem` naming the line it cannot read.
	bool ParseMermaid(Core::StringView text, MermaidChart& chart, Core::String& problem);

	// The ops that bring `chart` onto the board on `layer` (a layer id; "" the top one): `replace` lays everything out
	// anew; else nodes found by key, then by label, keep their places, new ones are laid out beside them, and the
	// connectors among the chart's nodes become the chart's. Notes say what it matched, added and kept.
	Core::String ImportOps(const Board& board, const MermaidChart& chart, Core::StringView layer, bool replace, Core::Array<Core::String>& notes);
}
