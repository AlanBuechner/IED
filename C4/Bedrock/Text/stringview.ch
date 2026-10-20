// StringView: a non-owning view over UTF-8 bytes - where they start and how many there are; it counts the characters
// they decode to when asked. It never allocates or frees; the owner of the bytes keeps them alive.
// A view of part of a string is not null-terminated, so read it through Data() and ByteLength().
//     Core::StringView s = Core::StringView("hello");

include "../Container/hash.ch"

namespace Core
{
	struct StringView
	{
		[operator("create")] public void Init(const char* text);                    // a null-terminated string
		[operator("create")] public void Init(const char* bytes, int64 byteLength);
		// Bytes whose character count is already known: no decoding pass.
		[operator("create")] public void Init(const char* bytes, int64 byteLength, int64 charCount);

		public const char* Data() const { return bytes; }
		public int64 ByteLength() const { return byteLength; }
		// Counted when asked (one pass over the bytes) unless the view was made with its count (BUG-288).
		public int64 CharCount() const { return charCount >= 0 ? charCount : Utf8CharCount(bytes, byteLength); }
		public bool IsEmpty() const { return byteLength == 0; }
		public bool IsValidUtf8() const { return Core::IsValidUtf8(bytes, byteLength); }
		public int32 ByteAt(int64 i) const { return (int32)((const uint8*)bytes)[i]; }
		// The code point at character (not byte) index charIndex; -1 when out of range or malformed.
		public int32 CodePointAt(int64 charIndex) const;

		// Byte index of the first `c` at or after `start`, or of `part`; -1 when absent.
		public int64 IndexOf(char c, int64 start) const;
		public int64 IndexOf(StringView part) const;
		public bool Contains(StringView part) const { return IndexOf(part) >= 0; }
		public bool StartsWith(StringView prefix) const;
		public bool EndsWith(StringView suffix) const;
		[operator("==")] public bool Equals(StringView other) const;
		[operator("!=")] public bool NotEquals(StringView other) const { return !Equals(other); }
		[operator("==")] public bool EqualsText(const char* text) const { return Equals(StringView(text)); }
		[operator("!=")] public bool NotEqualsText(const char* text) const { return !Equals(StringView(text)); }
		[operator("<")] public bool Less(StringView other) const;   // byte order
		[operator(">")] public bool Greater(StringView other) const { return other.Less(*this); }
		[operator("<=")] public bool LessOrEqual(StringView other) const { return !other.Less(*this); }
		[operator(">=")] public bool GreaterOrEqual(StringView other) const { return !Less(other); }
		[operator("hash")] public uint64 Hash() const { return Core::HashBytes(bytes, byteLength); }

		// Bytes [start, start + length) of this view, clamped to it; no allocation.
		public StringView Substring(int64 start, int64 length) const;
		// Without leading and trailing spaces, tabs and line breaks.
		public StringView Trimmed() const;

		private const char* bytes;
		private int64 byteLength;
		private int64 charCount;   // -1 when not known; a zero view's 0 is
	}

	// The number of characters in UTF-8 bytes: every byte that does not continue a sequence.
	int64 Utf8CharCount(const char* bytes, int64 byteLength);
	// Whether bytes are well-formed UTF-8 (Unicode 15, table 3-7): no overlong forms, surrogates or code
	// points above U+10FFFF.
	bool IsValidUtf8(const char* bytes, int64 byteLength);
	// Where the first ill-formed UTF-8 sequence starts, or -1 when the bytes are well-formed.
	int64 InvalidUtf8Offset(const char* bytes, int64 byteLength);
	// The code point of the UTF-8 sequence at byte `at`, its length into `width`: U+FFFD of width 1 for a byte that
	// does not start a well-formed sequence, -1 (width 0) at or past the end.
	int32 DecodeUtf8(const char* bytes, int64 byteLength, int64 at, int32& width);
	bool IsSpace(int32 byte);
}
