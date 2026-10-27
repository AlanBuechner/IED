// Text edits on line/column ranges and their rebase over the edits made since (docs/10 document editing). Positions
// are 0-based {line, col} with col in UTF-16 code units, as JavaScript strings index, so a browser and the server
// agree. The changes of one edit all refer to the text before it and must not overlap.

include "bedrock.ch"

namespace Ied
{
	struct Pos
	{
		public int64 m_Line;
		public int64 m_Col;
	}

	bool PosBefore(const Pos& a, const Pos& b);
	bool PosAtOrBefore(const Pos& a, const Pos& b);

	struct Change
	{
		public Pos m_Start;
		public Pos m_End;
		public Core::String m_Text;
	}

	// The changes of a JSON array of {range: {start: {line, col}, end}, text}; false with `error` when one is malformed.
	bool ParseChanges(const Serialization::DomNode& list, Core::Array<Change>& changes, Core::String& error);
	Core::String ChangesJson(const Core::Array<Change>& changes);
	// Where `text` ends.
	Pos EndPosition(Core::StringView text);
	// The text with every change made; false with `error` when one overlaps another or lies outside the text.
	bool ApplyChanges(Core::StringView text, const Core::Array<Change>& changes, Core::String& result, Core::String& error);
	// `changes`, made against an older text, moved over the change sets applied to it since (oldest first); false when
	// one overlaps a change made since.
	bool RebaseChanges(const Core::Array<Change>& changes, const Core::Array<Core::Array<Change>>& history, Core::Array<Change>& rebased);
}
