// The text of a PDF (ISO 32000-1 section 9): each page's content stream run for its text-showing operators, every string
// decoded through its font's ToUnicode CMap, else its encoding and glyph names; lines and spaces from where the text is
// placed. What web_fetch reads a PDF as.

include "PdfDocument.ch"

namespace WebFetch
{
	// The pages' text in their order, each after a "[Page N]" line, with a line break where text moves to another line
	// and a space where it skips ahead on one; the document's /Title as `title` ("" without one). False, with `error`, when
	// it is no PDF, it is encrypted, or it has no page.
	bool PdfToText(Core::StringView data, Core::String& text, Core::String& title, Core::String& error);

	// A PDF text string as UTF-8: UTF-16BE after its byte order mark, else PDFDocEncoding (read as Windows-1252).
	Core::String PdfTextString(Core::StringView bytes);

	// What a glyph name stands for: "a" -> "a", "fi" -> "fi", "quoteright" -> U+2019, "eacute" -> "e" and U+0301,
	// "uni00E9" -> U+00E9, "f_f_i" -> "ffi", "a.sc" -> "a"; "" when unknown.
	Core::String GlyphText(Core::StringView name);

	// A font's codes as text, and their widths.
	class PdfFont
	{
		// From the font's dictionary (simple fonts and Type0 fonts with their descendant).
		public void Load(PdfDocument& document, const PdfValue* font);
		// The text of a shown string's codes appended to `out`; their widths, in thousandths of the font's size, added to
		// `width`; how many codes, and how many of them single-byte spaces, into `codes` and `spaces`.
		public void Show(Core::StringView bytes, Core::StringBuilder& out, double& width, int64& codes, int64& spaces) const;

		private void ReadCMap(Core::StringView cmap);
		private void ReadEncoding(PdfDocument& document, const PdfValue* encoding);
		private void ReadWidths(PdfDocument& document, const PdfValue* font);
		private void ReadCidWidths(PdfDocument& document, const PdfValue* font);
		private double WidthOf(uint32 code) const;

		private int32 m_CodeBytes = 1;
		private Core::HashMap<uint32, Core::String> m_ToUnicode;
		private Core::Array<Core::String> m_Simple;   // a simple font's 256 codes as text
		private Core::Array<double> m_Widths;         // from m_FirstChar on
		private int64 m_FirstChar;
		private Core::HashMap<uint32, double> m_CidWidths;
		private double m_DefaultWidth = 500.0;
	}
}
