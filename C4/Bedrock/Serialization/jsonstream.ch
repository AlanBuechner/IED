// JsonStream: JSON read while it is still arriving (roadmap 3). A pull parser fed with chunks of text: Next hands
// back one event at a time, and NeedMore when the text so far ends inside one, so a large file is decoded while
// the rest is still being read and a pipe's messages as they come. It is as strict as ReadJson (the same tokens,
// a leading byte order mark skipped, nesting up to kMaxJsonDepth) and fails with ReadJson's errors at the same
// places; text that is not UTF-8 is an error as soon as it is fed. ReadValue collects the next whole value into a
// DomDoc - one element of a large array, say - and SkipValue passes over one, each across as many feeds as it takes.
//     Serialization::JsonStream json;
//     json.Feed(chunk);      // each chunk as it arrives, then
//     json.Finish();         // the text has ended
//     for (Serialization::JsonEvent e = json.Next(); e != Serialization::JsonEvent::End; e = json.Next()) {
//         if (e == Serialization::JsonEvent::NeedMore) { /* Feed or Finish, then ask again */ }
//         if (e == Serialization::JsonEvent::Error) { printf("%s\n", json.Failure().error); break; }
//     }

include "dom.ch"
include "jsonreader.ch"
include "Container/array.ch"

namespace Serialization
{
	enum class JsonEvent {
		NeedMore,                                   // the text so far ends inside an event: Feed or Finish and ask again
		End,                                        // the value is complete and the text ended after it
		Error,                                      // Failure() says what and where; every later call is Error too
		BeginObject, EndObject, BeginArray, EndArray,
		Key,                                        // a member's name, in Text()
		Null, Bool, Int, Float, String,             // a value: BoolValue(), IntValue(), FloatValue(), Text()
		Value                                       // ReadValue or SkipValue finished a whole value
	}

	// What the stream expects next.
	enum class JsonExpect { Value, ValueOrEnd, KeyOrEnd, Key, Colon, CommaOrEnd, Nothing }

	class JsonStream
	{
		[operator("create")] public void Init() {}

		// More text, after what the stream already holds.
		public void Feed(Core::StringView text);
		// No more text will come: a number or word the text ends with is complete, a value cut short is an error.
		public void Finish();
		// Ends the stream with `why` as its error, at the end of the text so far: for a source that failed.
		public void Stop(const char* why);

		public JsonEvent Next();
		// The next whole value into `doc`: Value, with `node` its root. Where no value stands, the event that does:
		// Key, EndObject, EndArray, End, Error, or NeedMore - the part read so far is kept, so feed the stream and call
		// again with the same doc.
		public JsonEvent ReadValue(DomDoc& doc, DomNode& node);
		// ReadValue without keeping anything.
		public JsonEvent SkipValue();

		public Core::StringView Text() const { return text.View(); }   // a Key's or String's text, until the next call
		public bool BoolValue() const { return boolean; }
		public int64 IntValue() const { return integer; }
		public double FloatValue() const { return real; }
		// The objects and arrays open after the last event.
		public int64 Depth() const { return containers.Count(); }
		// After Error: what is wrong and where (offset, line and column in the whole text); the root is invalid.
		public ReadResult Failure() const;

	private:
		private JsonEvent Step();
		private JsonEvent ValueToken();
		private JsonEvent Container(bool object);
		private JsonEvent Close(bool object);
		private JsonEvent StringToken(bool key);
		private JsonEvent NumberToken();
		private JsonEvent Word(const char* word, JsonEvent event);
		private JsonEvent EndOfText();
		private JsonEvent AfterValue(JsonEvent event);
		private bool Collect(JsonEvent event, DomDoc& doc, DomNode& node);
		private DomNode Made(JsonEvent event, DomDoc& doc);
		private bool InObject() const { return !containers.IsEmpty() && containers.Back(); }
		private bool SkipMark();
		private void SkipSpace();
		private void Validate();
		private void Compact();
		private Core::StringView Available() const { return pending.View().Substring(0, checked); }
		private JsonEvent Fail(int64 at, const char* message);

		private Core::StringBuilder pending;    // the fed text not yet read past; its byte 0 is byte `base` of the whole
		private Core::StringBuilder spare;      // the next `pending`, when the read part is dropped
		private int64 base;
		private int64 position;                 // in `pending`
		private int64 checked;                  // `pending`'s bytes known to be UTF-8
		private int64 scanned;                  // a string cut short: how far its end was looked for
		private bool finished;
		private bool marked;                    // a byte order mark has been looked for
		private JsonExpect expect;
		private Core::Array<bool> containers;   // the open ones, innermost last: true for an object
		private int64 line = 1;                 // of `position`, and where that line starts in the whole text
		private int64 lineStart;
		private Core::StringBuilder text;
		private bool boolean;
		private int64 integer;
		private double real;
		private const char* error;
		private int64 errorAt;
		private Core::Array<DomNode> open;      // ReadValue's containers so far, outermost first
		private Core::String key;               // and the member name its next value goes under
		private int64 skipDepth;                // SkipValue's containers so far
	}
}
