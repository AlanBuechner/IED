// YAML text of a DOM tree.

include "json.ch"

namespace Serialization
{
	// `node` as block-style YAML with `indent` spaces per level (at least 2). Strings and keys are
	// double-quoted, so every scalar reads back as written; an empty object or array is `{}` / `[]`, and NaN
	// and the infinities are `.nan`, `.inf` and `-.inf`.
	void WriteYaml(const DomNode& node, Core::StringBuilder* out, int32 indent);
}
