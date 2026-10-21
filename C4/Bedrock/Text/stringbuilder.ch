// StringBuilder: a growable buffer for composing text - log lines, formatted values, generated files.
// It stays null-terminated; View() is a view of the current contents and ToString() a String copy. A
// zero builder (`Core::StringBuilder b;`) is empty and allocates on its first append; a copy owns its
// own buffer.

include "string.ch"

namespace Core
{
	class StringBuilder
	{
		[operator("create")] public void Init(int64 capacity);   // empty, with room for `capacity` bytes
		[operator("copy")] public void CopyFrom(const StringBuilder& other);
		[operator("delete")] public void Delete();

		public void Append(StringView text);
		public void Append(const char* text) { Append(StringView(text)); }
		public void AppendByte(int32 byte);
		// The UTF-8 bytes of a code point; a surrogate or anything past U+10FFFF is written as U+FFFD.
		public void AppendCodePoint(int32 codePoint);
		public void AppendBytes(const char* bytes, int64 count);   // raw bytes, no terminator searched for
		public void AppendInt(int64 value) { AppendIntBase(value, 10, 0); }
		public void AppendUInt(uint64 value) { AppendUIntBase(value, 10, 0); }
		// Base 2 to 16 (anything else is 10), lowercase digits, zero-padded to at least minWidth digits.
		public void AppendIntBase(int64 value, int32 base, int32 minWidth);
		public void AppendUIntBase(uint64 value, int32 base, int32 minWidth);
		public void AppendHex(uint64 value) { AppendUIntBase(value, 16, 0); }
		public void AppendBin(uint64 value) { AppendUIntBase(value, 2, 0); }
		// Rounded to `decimals` fraction digits; a negative `decimals` writes the shortest text that reads
		// back as the same value. Infinities and NaN are written as `inf` and `nan`.
		public void AppendDouble(double value, int32 decimals);
		public void AppendFloat(float value, int32 decimals) { AppendDouble((double)value, decimals); }

		public void Clear() { length = 0; if (buffer) buffer[0] = (char)0; }
		// Keeps the first `byteLength` bytes; a longer length changes nothing.
		public void Truncate(int64 byteLength) { if (byteLength >= 0 && byteLength < length) { length = byteLength; buffer[length] = (char)0; } }
		public void Reserve(int64 bytes);
		public int64 ByteLength() const { return length; }
		public bool IsEmpty() const { return length == 0; }
		public const char* CStr() const { return buffer ? buffer : ""; }   // valid until the next change
		public StringView View() const { return StringView(CStr(), length); }   // valid until the next change
		public String ToString() const { return String(View()); }

		private void AppendDecimal(uint64 value, int32 minWidth);

		private char* buffer;   // null-terminated at [length]; null for the zero builder
		private int64 length;
		private int64 capacity; // the terminator excluded
	}
}
