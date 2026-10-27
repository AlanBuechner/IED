// The text a converted page is written into (docs/29 "Conversion"): white space collapsed outside preformatted text,
// line breaks between blocks asked for and written only before the next text (so empty blocks leave nothing), a line
// prefix for block quotes and list items, and opening marks (**, _, `, [, a heading's #) held until text follows, so an
// element with no text leaves no marks either.

include "bedrock.ch"

namespace WebFetch
{
	class PageWriter
	{
		[operator("create")] public void Init() {}

		// Text as the page has it: runs of white space are one space, or kept whole while `preformatted`.
		public void Text(Core::StringView text);
		// A mark that goes right before the next text.
		public void Open(Core::StringView mark);
		// A closing mark: written now, or taken back with its opening when no text came since.
		public void Close(Core::StringView mark);
		// Takes back `mark` when no text came since it opened; whether it did.
		public bool Discard(Core::StringView mark);
		// Text written as it is where the next text would go (an image, a table cell's "| ").
		public void Inline(Core::StringView text);
		// `lines` line breaks (1: a new line, 2: a blank line) before whatever comes next; a space inside a table cell.
		public void Break(int32 lines);
		// Bytes right after what is written, with no break, prefix or space before them.
		public void Append(Core::StringView text);
		public bool EndsWithNewline() const;
		// The text, with trailing spaces and runs of blank lines removed.
		public Core::String Finish() const;

		public int32 m_QuoteDepth;
		public int32 m_ListDepth;
		public Core::String m_ItemMarker;   // "- " or "3. ": starts the next line, once
		public int32 m_CellDepth;
		public bool m_Preformatted;
		public bool m_Plain;

		private void Flush();
		private void LinePrefix();

		private Core::StringBuilder m_Out;
		private Core::StringBuilder m_Marks;
		private int32 m_Breaks;
		private bool m_Space;
		private bool m_LineStart = true;
	}
}
