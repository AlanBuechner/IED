// Positions between IED's clients and a language server (docs/24 "Positions"): clients count UTF-16 code units on a
// line, a server UTF-16 or UTF-8 bytes. Lines end at "\n", a "\r" before it being part of the line, as in docs/10.

include "bedrock.ch"

namespace Ied
{
	enum class PositionEncoding { Utf16, Utf8 }

	// "utf-16", "utf-8" (or "bytes"); false for anything else.
	bool ParsePositionEncoding(Core::StringView name, PositionEncoding& encoding);
	const char* PositionEncodingName(PositionEncoding encoding);

	// The column `col` (in `from` units) of `line` in `to` units. A column past the line's end stays past it by as much;
	// one inside a character (half a surrogate pair, a byte inside a sequence) moves to that character's end.
	int64 ConvertColumn(Core::StringView line, int64 col, PositionEncoding from, PositionEncoding to);

	// A text's lines, found once.
	class LineIndex
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Core::StringView text);

		public int64 Count() const { return starts.Count(); }
		// Line `n` without its "\n"; empty past the last line.
		public Core::StringView Line(int64 n) const;

		private Core::String text;
		private Core::Array<int64> starts;
	}
}
