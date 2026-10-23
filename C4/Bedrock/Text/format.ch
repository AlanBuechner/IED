// Format: positional formatting into a caller-owned StringBuilder, each argument written as its own type.
//
// A placeholder is {index} or {index:kind.width}. Indices name the arguments in call order, may come in any order
// and may repeat; `{{` and `}}` are literal braces.
//
//   argument                                     {n}                       kind               .N
//   an integer (int8 to int64, uint8 to uint64)  decimal                   d D u decimal,     zero-pad to N digits
//                                                                          x hexadecimal,
//                                                                          X with A to F,
//                                                                          b binary, o octal
//   float, double                                the shortest text that    f F fixed point    N fraction digits
//                                                reads back as the value                      (6 when not given,
//                                                                                             as C's %f)
//   const char*, Core::String, Core::StringView  the text                  s
//   bool                                         true or false
//   char                                         the character
//
//   Core::Format(&b, "id={0:d.4} val={1:x.8} again={0}", 255, 4096);   // "id=0255 val=00001000 again=255"
//   Core::Format(&b, "{0} is {1} bytes", name, size);                  // a String and an int64, as they are
//
// A kind of another family (`{0:x}` of a double, `{0:s}` of an int) writes the argument as {n} does. A placeholder
// that names no argument, or a malformed one, is written as text. Arguments are taken by their types, never read as
// raw bits, so a wrong one is a compile error (an object Format cannot write has no FormatArgument).

include "string.ch"
include "stringbuilder.ch"
include "stringview.ch"

namespace Core
{
	// What a FormatArgument holds.
	enum class FormatValue : int32 { Signed, Unsigned, Float, Double, Text, Bool, Char }

	// One argument of Format, with what it is: made from each argument by the Format template.
	struct FormatArgument
	{
		[operator("create")] public void Init(int8 value) { kind = FormatValue::Signed; integer = (int64)value; }
		[operator("create")] public void Init(int16 value) { kind = FormatValue::Signed; integer = (int64)value; }
		[operator("create")] public void Init(int32 value) { kind = FormatValue::Signed; integer = (int64)value; }
		[operator("create")] public void Init(int64 value) { kind = FormatValue::Signed; integer = value; }
		[operator("create")] public void Init(uint8 value) { kind = FormatValue::Unsigned; integer = (int64)value; }
		[operator("create")] public void Init(uint16 value) { kind = FormatValue::Unsigned; integer = (int64)value; }
		[operator("create")] public void Init(uint32 value) { kind = FormatValue::Unsigned; integer = (int64)value; }
		[operator("create")] public void Init(uint64 value) { kind = FormatValue::Unsigned; integer = (int64)value; }
		[operator("create")] public void Init(float value) { kind = FormatValue::Float; real = (double)value; }
		[operator("create")] public void Init(double value) { kind = FormatValue::Double; real = value; }
		[operator("create")] public void Init(bool value) { kind = FormatValue::Bool; integer = value ? 1 : 0; }
		[operator("create")] public void Init(char value) { kind = FormatValue::Char; integer = (int64)value; }
		[operator("create")] public void Init(const char* value) { kind = FormatValue::Text; text = StringView(value ? value : ""); }
		[operator("create")] public void Init(StringView value) { kind = FormatValue::Text; text = value; }
		[operator("create")] public void Init(const String& value) { kind = FormatValue::Text; text = value.View(); }

		public FormatValue kind;
		public int64 integer;   // a signed integer, an unsigned one's bits, a bool or a char
		public double real;
		public StringView text;
	}

	// The text of `format` with each placeholder replaced by the argument it names, of `count` arguments.
	void FormatArguments(StringBuilder* out, const char* format, const FormatArgument* arguments, int64 count);

	// Arguments by reference: a String is read where it is, never copied.
	template<...Ts> void Format(StringBuilder* out, const char* format, const Ts&... args) {
		FormatArgument[sizeof...(Ts)] arguments = { FormatArgument(args)... };
		FormatArguments(out, format, &arguments[0], sizeof...(Ts));
	}

	void Format(StringBuilder* out, const char* format) { FormatArguments(out, format, nullptr, 0); }
}
