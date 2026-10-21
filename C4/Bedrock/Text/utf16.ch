// UTF-16: for the Win32 W functions, and for protocols that count text in UTF-16 units (an LSP position's
// `character`). C4 text is UTF-8; these convert between the two and between their offsets. Ill-formed input - UTF-8
// that is not, an unpaired surrogate - becomes U+FFFD.
//     Core::Array<char16> wide = Core::ToUtf16(path.View(), true);   // null-terminated, for a W function
//     int64 byte = Core::ByteOffsetOfUtf16(line, position.character);

include "string.ch"
include "stringview.ch"
include "Container/array.ch"

namespace Core
{
	// `text` as UTF-16 code units, with a null unit after them when `terminate`.
	Array<char16> ToUtf16(StringView text, bool terminate = false);
	// `count` UTF-16 units as UTF-8.
	String FromUtf16(const char16* units, int64 count);
	// How many UTF-16 units `text` takes.
	int64 Utf16Length(StringView text);
	// The UTF-16 offset of byte `byteOffset` of `text`: the units of the characters before it (a byte inside a
	// character counts from that character's start; past the end is the whole length).
	int64 Utf16OffsetOfByte(StringView text, int64 byteOffset);
	// The byte offset of UTF-16 offset `units` in `text`: the start of the character that unit belongs to (the
	// second unit of a surrogate pair is its character's start); past the end is the byte length.
	int64 ByteOffsetOfUtf16(StringView text, int64 units);
}
