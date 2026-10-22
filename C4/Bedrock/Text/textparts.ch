// Text helpers: splitting at a separator, ASCII case and reading a real number.
//
//     Core::Array<Core::StringView> fields = line.View().Split(',');   // views into line: no copies
//     Core::Array<Core::String> words = text.Split(" ");               // a String splits into Strings of its own
//     Core::SplitParts parts = Core::SplitParts(csv, ",");             // walks the parts without allocating
//     Core::StringView part;
//     while (parts.Next(part)) ...
//     Core::String upper = name.ToUpper();
//     double ratio = 0;
//     if (!Core::ParseDouble(text, ratio)) ...

include "string.ch"
include "Container/array.ch"

namespace Core
{
	// The parts of a text between occurrences of a separator, in order: n separators give n + 1 parts, empty ones
	// included ("a,,b" is "a", "", "b" and "" is one empty part). An empty separator gives the whole text.
	struct SplitParts
	{
		[operator("create")] public void Init(StringView text, StringView separator);
		[operator("create")] public void Init(StringView text, char separator);
		[operator("create")] public void Init(StringView text, const char* separator) { Init(text, StringView(separator)); }
		public bool Next(StringView& part);

		private StringView text;
		private StringView separator;
		private int32 single;   // the separator byte, or -1 when it is `separator`
		private int64 at;
		private bool done;
	}

	Array<StringView> Split(StringView separator) extends StringView;
	Array<StringView> Split(const char* separator) extends StringView;
	Array<StringView> Split(char separator) extends StringView;
	Array<String> Split(StringView separator) extends String;
	Array<String> Split(const char* separator) extends String;
	Array<String> Split(char separator) extends String;

	// ASCII letters changed; every other byte, UTF-8 sequences included, kept.
	String ToLower() extends StringView;
	String ToUpper() extends StringView;
	String ToLower() extends String;
	String ToUpper() extends String;

	// The whole text as a double in strtod's syntax (digits, a fraction, an exponent, `inf`, `nan`, hexadecimal);
	// false, with `value` as it was, for any other text, leading or trailing spaces included.
	bool ParseDouble(StringView text, double& value);
}
