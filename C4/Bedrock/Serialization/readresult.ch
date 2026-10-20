// ReadResult: what reading a text format into a DOM gives back - the root node, or where and why the text
// is not valid. Shared by the JSON and YAML readers.

include "dom.ch"

namespace Serialization
{
	struct ReadResult {
		public DomNode root;          // invalid when the read failed
		public const char* error;     // null on success; otherwise what is wrong, as static text
		public int64 offset;          // the byte the error is at
		public int32 line;            // 1-based
		public int32 column;          // 1-based, in bytes

		public bool Ok() const { return error == nullptr; }
	}

	// A failed result for the error at `offset` in `text`, with its line and column.
	ReadResult ReadFailure(Core::StringView text, int64 offset, const char* error);
	// A successful result.
	ReadResult ReadSuccess(DomNode root);

	bool IsDecimalDigit(int32 byte);
	// The value of a hex digit, or -1.
	int32 HexDigitValue(int32 byte);
}
