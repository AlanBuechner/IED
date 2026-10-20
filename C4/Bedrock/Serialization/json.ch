// JSON text of a DOM tree.

include "dom.ch"

namespace Serialization
{
	// `node` as JSON: compact with an indent of 0, otherwise one value per line with `indent` spaces per
	// level. Floats are written so they read back as the same double and as floats (`1.0`, not `1`); NaN and
	// the infinities, which JSON cannot express, are written as null.
	void WriteJson(const DomNode& node, Core::StringBuilder* out, int32 indent);

	// A JSON string literal of `text`: quotes, backslashes and control characters escaped.
	void WriteJsonString(Core::StringView text, Core::StringBuilder* out);

	// The shortest text that reads back as `value`, with a fraction or exponent so it reads back as a float.
	void WriteFloatText(double value, Core::StringBuilder* out);
}
