// What a YAML plain scalar means under the core schema, and the escapes of a double-quoted one.

include "dom.ch"

namespace Serialization
{
	// The node a plain scalar's text resolves to: null, bool, int, float or string.
	DomNode ResolveYamlPlain(Core::StringView text, DomDoc& doc);

	// The bytes a double-quoted escape stands for (the character after the backslash and what follows it),
	// appended to `out`. Returns how many bytes after the backslash it used, or 0 when it is not an escape.
	int64 AppendYamlEscape(Core::StringView rest, Core::StringBuilder& out);
}
