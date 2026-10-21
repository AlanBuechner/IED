// The tokens of JSON both readers share (ReadJson, JsonStream), so they accept and refuse the same text: strings
// and numbers, each read from `at` in `text` with `at` moved past it. A malformed one sets `fault` - what is wrong,
// as static text, and the byte it is at - and returns false.

include "readresult.ch"

namespace Serialization
{
	struct JsonFault {
		public const char* error;
		public int64 at;
	}

	// A number: an integer when it has no fraction or exponent and fits an int64, else a double.
	struct JsonNumber {
		public bool integral;
		public int64 integer;
		public double real;
	}

	// The string whose opening quote is at `at`, its escapes decoded (surrogate pairs into one character), appended
	// to `out`.
	bool ReadJsonString(Core::StringView text, int64& at, Core::StringBuilder& out, JsonFault& fault);
	// The same string as a view: of `text` itself when it has no escape (nothing is copied), else of `scratch`, which
	// it is decoded into. Valid until `scratch` changes.
	bool ReadJsonStringView(Core::StringView text, int64& at, Core::StringBuilder& scratch, Core::StringView& out, JsonFault& fault);
	// The first byte at or after `at` that a JSON string cannot hold as it is - '"', '\' or a control character - or
	// `end`; sixteen bytes are checked at a time.
	int64 PlainStringRunEnd(const char* bytes, int64 at, int64 end);
	// The number that starts at `at` (a '-' or a digit). It ends where the grammar does: "1.5.3" reads 1.5.
	bool ReadJsonNumber(Core::StringView text, int64& at, JsonNumber& number, JsonFault& fault);
}
