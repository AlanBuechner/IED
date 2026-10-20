// JsonWriter: JSON text written front to back - the frames the server sends and the results of its methods.
//     Ied::JsonWriter json;
//     json.BeginObject();
//     json.Field("t", "pong");
//     json.EndObject();

include "bedrock.ch"

namespace Ied
{
	class JsonWriter
	{
		[operator("create")] public void Init() {}

		public void BeginObject() { Open('{'); }
		public void EndObject() { Close('}'); }
		public void BeginArray() { Open('['); }
		public void EndArray() { Close(']'); }
		public void Key(Core::StringView key);
		public void String(Core::StringView value);
		public void Int(int64 value);
		public void Bool(bool value);
		public void Null();
		// JSON text written as it is: a value encoded before.
		public void Raw(Core::StringView json);

		public void Field(const char* key, Core::StringView value) { Key(Core::StringView(key)); String(value); }
		public void Field(const char* key, const char* value) { Key(Core::StringView(key)); String(Core::StringView(value)); }
		public void IntField(const char* key, int64 value) { Key(Core::StringView(key)); Int(value); }
		public void BoolField(const char* key, bool value) { Key(Core::StringView(key)); Bool(value); }
		public void RawField(const char* key, Core::StringView json) { Key(Core::StringView(key)); Raw(json); }

		public Core::StringView View() const { return text.View(); }
		public Core::String ToString() const { return text.ToString(); }

	private:
		private void Open(int32 bracket);
		private void Close(int32 bracket);
		private void BeforeValue();

		private Core::StringBuilder text;
		private Core::Array<bool> empty;   // per open container: nothing written in it yet
		private bool afterKey;
	}

	// `text` as a JSON string, quotes included.
	Core::String JsonString(Core::StringView text);
}
