// A PDF file's objects (ISO 32000-1 section 7), read for their text: values, the indirect objects - those written out
// and those packed in object streams - their streams decoded (FlateDecode) when asked for, and the trailer's catalog
// and document information. Nothing is written; encrypted files, images and font programs are not read.

include "bedrock.ch"

namespace WebFetch
{
	enum class PdfKind { Null, Bool, Number, Name, String, Array, Dict, Ref }

	// One value; an array keeps its elements in m_Items, a dictionary its keys in m_Keys and their values in m_Items.
	struct PdfValue
	{
		public PdfKind m_Kind;
		public double m_Number;
		public Core::String m_Text;   // a name without its slash, or a string's bytes
		public int64 m_Object;        // what a reference names
		public Core::Array<PdfValue> m_Items;
		public Core::Array<Core::String> m_Keys;

		// A dictionary's value for `key`; null when absent or not a dictionary.
		public const PdfValue* Get(Core::StringView key) const;
		public bool IsName(Core::StringView name) const { return m_Kind == PdfKind::Name && m_Text.View() == name; }
	}

	// Reads values and keywords from PDF syntax: objects, content streams and CMaps.
	class PdfParser
	{
		[operator("create")] public void Init(Core::StringView data, int64 at) { m_Data = data; m_At = at; }

		// The next value into `value`; false at the end, or at a keyword, which `keyword` then holds (and is consumed).
		// `references`: "1 0 R" is a reference (objects), not three operands (content streams).
		public bool Next(PdfValue& value, Core::StringView& keyword, bool references);
		public void SkipSpace();
		public int64 Position() const { return m_At; }
		public void MoveTo(int64 at) { m_At = at; }
		public bool AtEnd() const { return m_At >= m_Data.ByteLength(); }

		private bool Value(PdfValue& value, Core::StringView& keyword, bool references, int32 depth);
		private Core::StringView Word();
		private void Name(PdfValue& value);
		private void Literal(PdfValue& value);
		private void Hex(PdfValue& value);

		private Core::StringView m_Data;
		private int64 m_At;
	}

	struct PdfObject
	{
		public PdfValue m_Value;
		public int64 m_StreamStart = -1;   // the stream's bytes in the file, -1 for none
		public int64 m_StreamLength;
		public bool m_Decoded;
		public bool m_Undecodable;          // a filter other than FlateDecode, or data that does not inflate
		public Core::Array<uint8> m_Data;   // the decoded stream, once asked for
	}

	class PdfDocument
	{
		// Every object of the file at `data` (which must outlive the document); false, with `error`, when it is no PDF
		// or encrypted.
		public bool Parse(Core::StringView data, Core::String& error);
		public const PdfObject* Object(int64 number) const;
		// `value` with its reference followed (a chain of them too); null for a reference to nothing.
		public const PdfValue* Resolve(const PdfValue* value) const;
		// A dictionary's value for `key`, its reference followed.
		public const PdfValue* Lookup(const PdfValue* dictionary, Core::StringView key) const;
		// The decoded stream of object `number`, or of the object a reference names; null without one or when it cannot be
		// decoded.
		public const Core::Array<uint8>* Stream(int64 number);
		public const Core::Array<uint8>* Stream(const PdfValue* reference);
		public const PdfValue* Catalog() const { return Resolve(m_Trailer.Get(Core::StringView("Root"))); }
		public const PdfValue* Info() const { return Resolve(m_Trailer.Get(Core::StringView("Info"))); }
		public int64 ObjectCount() const { return m_Objects.Count(); }
		// Object numbers in ascending order.
		public Core::Array<int64> ObjectNumbers() const;

		// The object whose value starts at `valueStart`, and its stream's place; `next` is where reading goes on.
		private void ReadObjectAt(int64 valueStart, int64 number, int64& next);
		private void ReadTrailers();
		private void UnpackObjectStreams();
		private bool Decode(PdfObject& object);

		private Core::StringView m_Data;
		private Core::HashMap<int64, PdfObject*> m_Objects;
		private PdfValue m_Trailer;
	}
}
