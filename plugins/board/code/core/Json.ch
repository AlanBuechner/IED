// JSON written as it goes, for the board's files, events and tool answers: numbers rounded to a tenth (board
// coordinates are kept to one decimal), strings escaped, and a line break where a caller asks for one so a board file
// holds one element per line.

include "bedrock.ch"
include "math.ch"

namespace Boards
{
	class JsonOut
	{
		[operator("create")] public void Init() {}

		public void BeginObject() { Open('{'); }
		public void EndObject() { Close('}'); }
		public void BeginArray() { Open('['); }
		public void EndArray() { Close(']'); }
		public void Key(Core::StringView key);
		public void Str(Core::StringView value);
		public void Num(double value);
		public void Int(int64 value);
		public void Bool(bool value);
		public void Null();
		// JSON text written as it is: a value encoded before.
		public void Raw(Core::StringView json);
		// A line break before the next value or the closing bracket.
		public void Newline() { m_Text.AppendByte('\n'); }

		public void StrField(const char* key, Core::StringView value) { Key(Core::StringView(key)); Str(value); }
		public void NumField(const char* key, double value) { Key(Core::StringView(key)); Num(value); }
		public void IntField(const char* key, int64 value) { Key(Core::StringView(key)); Int(value); }
		public void BoolField(const char* key, bool value) { Key(Core::StringView(key)); Bool(value); }
		public void RawField(const char* key, Core::StringView json) { Key(Core::StringView(key)); Raw(json); }

		public Core::StringView View() const { return m_Text.View(); }
		public Core::String ToString() const { return m_Text.ToString(); }

		private void Open(int32 bracket);
		private void Close(int32 bracket);
		private void BeforeValue();

		private Core::StringBuilder m_Text;
		private Core::Array<bool> m_Empty;
		private bool m_AfterKey;
	}

	// `value` rounded to a tenth, without a fraction when it is whole ("12", "12.5", "-0.3").
	void AppendNumber(Core::StringBuilder& out, double value);
	// `text` as a JSON string, quotes included.
	void AppendJsonString(Core::StringBuilder& out, Core::StringView text);
	// `value` rounded to a tenth.
	double Tenth(double value);
	// `value` as AppendNumber writes it.
	Core::String NumberText(double value);
}
