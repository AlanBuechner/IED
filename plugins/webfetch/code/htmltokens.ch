// HTML as web_fetch reads it (docs/29 "Conversion"): a tolerant tokenizer - start and end tags with their attributes,
// and text with character references decoded - that never fails on what browsers accept. Comments, doctypes and
// processing instructions are dropped, and so is the content of script and style; the text of title and textarea is
// kept as text (it is not markup).

include "bedrock.ch"

namespace WebFetch
{
	enum class TokenKind { Text, Start, End }

	struct HtmlAttribute
	{
		public Core::String name;    // lowercase
		public Core::String value;   // character references decoded
	}

	struct HtmlToken
	{
		public TokenKind kind;
		public Core::String name;    // a tag's, lowercase
		public Core::String text;    // a Text token's, decoded
		public Core::Array<HtmlAttribute> attributes;
		public bool selfClosing;     // "<x/>"
	}

	Core::Array<HtmlToken> Tokenize(Core::StringView html);
	// A start tag's attribute value; "" when it has none of that name.
	Core::StringView AttributeOf(const HtmlToken& token, const char* name);
	bool HasAttribute(const HtmlToken& token, const char* name);
	// `text` with its character references (&amp; &#233; &#xE9; &eacute;) decoded to UTF-8; one that names nothing is
	// kept as written.
	Core::String DecodeEntities(Core::StringView text);
	// The code point a named character reference stands for ("eacute" -> 233), or -1.
	int32 NamedEntity(Core::StringView name);
}
