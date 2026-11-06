// The board as a diagram (docs/32 "Mermaid"): its nodes with stable keys, the connectors among them, the frames that
// hold them, and the notes beside them - what the Mermaid and Markdown exports are written from.

include "Board.ch"

namespace Boards
{
	class DiagramNode
	{
		public Element* m_Element;
		public Core::String m_Key;
		public Core::String m_Frame;   // the innermost frame holding it ("" for none)
	}

	class DiagramFrame
	{
		public Element* m_Element;
		public Core::String m_Key;
		public Core::String m_Parent;  // the frame holding this one
	}

	class Diagram
	{
		public Core::Array<DiagramNode> m_Nodes;
		public Core::Array<DiagramFrame> m_Frames;
		public Core::Array<Element*> m_Edges;
		public Core::Array<Element*> m_Notes;    // notes and free text
		public Core::Array<Element*> m_Strokes;  // ink not in the diagram
		public Core::HashMap<Core::String, Core::String> m_KeyOf;   // element id -> key
		public bool m_Horizontal;                // its connectors run more across than down

		public Core::StringView Key(Core::StringView id) const;
		public const DiagramNode* Node(Core::StringView id) const;
	}

	// The diagram of the elements among `ids` (empty: every element on a visible layer), keys made unique: an
	// element's own key, else one from its text ("API Gateway" -> api_gateway), else its id.
	Diagram ExtractDiagram(const Board& board, const Core::Array<Core::String>& ids);
	// A Mermaid id from text: lower case letters, digits and '_', starting with a letter.
	Core::String KeyFromText(Core::StringView text);
	// A Mermaid flowchart of the diagram, in a stable order (nodes as they were made, edges by their ends): node colors as
	// classDef/class lines (`colors`: palette name -> #rrggbb; none written when it is empty), notes as comments by their
	// nearest node.
	Core::String ExportMermaid(const Board& board, const Diagram& diagram, const Core::HashMap<Core::String, Core::String>& colors);
	// The diagram as a Markdown outline: components with their descriptions, notes and connections; groups; loose
	// notes; the flowchart; what is still only ink.
	Core::String ExportOutline(const Board& board, const Diagram& diagram, const Core::HashMap<Core::String, Core::String>& colors);
	// "1 note", "3 notes".
	Core::String Counted(int64 count, const char* one, const char* many);
}
