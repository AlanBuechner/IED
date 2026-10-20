// YAML text into a DOM: one document of YAML 1.2 with the core schema - block mappings and sequences, flow
// `[...]` / `{...}` collections, plain, single- and double-quoted scalars, literal `|` and folded `>` block
// scalars, comments and the `---` / `...` markers. A plain scalar is null (`~`, `null`, empty), a bool
// (`true`, `False`), an int (`42`, `0x1F`, `0o17`), a float (`1.5`, `.inf`, `.nan`) or else a string; a quoted
// one is always a string. Anchors, aliases, tags, complex `?` keys, directives and further documents are
// errors that say so. A mapping keeps its keys in order, duplicates included (Member finds the first).
//     Serialization::DomDoc doc;
//     Serialization::ReadResult read = Serialization::ReadYaml(Core::StringView(text), doc);
//     if (!read.Ok()) printf("%d:%d %s\n", read.line, read.column, read.error);

include "readresult.ch"

namespace Serialization
{
	ReadResult ReadYaml(Core::StringView text, DomDoc& doc);

	// Collections nested deeper than this are an error, so a hostile document cannot exhaust the stack.
	const int32 kMaxYamlDepth = 512;
}
