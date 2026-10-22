// String: the owning UTF-8 string (std::string). It owns a null-terminated heap buffer, keeps its
// character count current and frees the buffer on delete; a copy owns its own buffer. A zero String is
// the empty string, so `Core::String s;` allocates nothing.
//     Core::String s = Core::String("hello");
//     s.Append(" world");
//     Core::StringView v = s.View();

include "stringview.ch"

namespace Core
{
	class String
	{
		[operator("create")] public void Init(const char* text);
		[operator("create")] public void Init(StringView text);
		[operator("create")] public void Init(int64 reserve);   // empty, with room for `reserve` bytes
		[operator("copy")] public void CopyFrom(const String& other);
		[operator("delete")] public void Delete();
		[operator("native_bytes")] public int64 NativeBytes() const { return buffer ? capacity + 1 : 0; }

		public int64 ByteLength() const { return length; }
		public int64 CharCount() const { return chars; }
		public int64 Capacity() const { return capacity; }
		public bool IsEmpty() const { return length == 0; }
		// The bytes, null-terminated; valid until the next change.
		public const char* CStr() const { return buffer ? buffer : ""; }
		public StringView View() const { return StringView(CStr(), length, chars); }
		public int32 CodePointAt(int64 charIndex) const { return View().CodePointAt(charIndex); }

		[operator("==")] public bool Equals(const String& other) const { return View() == other.View(); }
		[operator("!=")] public bool NotEquals(const String& other) const { return View() != other.View(); }
		[operator("==")] public bool EqualsView(StringView other) const { return View() == other; }
		[operator("!=")] public bool NotEqualsView(StringView other) const { return View() != other; }
		[operator("==")] public bool EqualsText(const char* text) const { return View() == text; }
		[operator("!=")] public bool NotEqualsText(const char* text) const { return View() != text; }
		[operator("<")] public bool Less(const String& other) const { return View() < other.View(); }
		[operator(">")] public bool Greater(const String& other) const { return View() > other.View(); }
		[operator("<=")] public bool LessOrEqual(const String& other) const { return View() <= other.View(); }
		[operator(">=")] public bool GreaterOrEqual(const String& other) const { return View() >= other.View(); }
		[operator("hash")] public uint64 Hash() const { return View().Hash(); }
		[operator("+")] public String Concat(StringView other) const;
		[operator("+")] public String ConcatText(const char* other) const { return Concat(StringView(other)); }
		[operator("+")] public String ConcatString(const String& other) const { return Concat(other.View()); }

		[operator("+=")] public void Append(StringView text);
		[operator("+=")] public void Append(const char* text) { Append(StringView(text)); }
		[operator("+=")] public void Append(const String& text) { Append(text.View()); }
		public void Clear();                  // empty, keeping the buffer
		// Room for at least `bytes` without growing again. Inline: every append asks, and only growth costs a call.
		public inline void Reserve(int64 bytes) { if (bytes > capacity || !buffer) Grow(bytes); }

		private void Grow(int64 bytes);   // room for `bytes`, at least twice what it had

		private char* buffer;   // null-terminated at [length]; null for the zero String
		private int64 length;   // bytes, the terminator excluded
		private int64 capacity; // bytes the buffer holds, the terminator excluded
		private int64 chars;    // decoded characters
	}
}
