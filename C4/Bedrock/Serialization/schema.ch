// SchemaRegistry: JSON Schema documents loaded by name, their $refs resolved within and across files (B68). The
// dialect is a draft 2020-12 subset - the keywords IED's tools/pylib/minischema.py knows, which are those tools'
// schemas use (MCP servers' among them): every assertion but the unevaluated* ones, $dynamicRef and the
// content* vocabulary - and loading refuses any other keyword (an "x-" one is an annotation), so a schema never relies
// on a check nothing makes. Patterns (`pattern`, the names of `patternProperties`) are Core::Regex (RE2 syntax),
// searched for anywhere in the string; one that does not compile is refused on load. `definitions` is read as
// draft 7's name of `$defs`.
//     Serialization::SchemaRegistry schemas;
//     if (!schemas.Load(Core::StringView("shared/host-api/schemas/project.v1.schema.json"), error)) ...
//     Core::Array<Serialization::SchemaProblem> problems = Serialization::ValidateJson(schemas, name, "", value);

include "jsonc.ch"
include "Text/regex.ch"
include "Container/hashmap.ch"

namespace Serialization
{
	class SchemaRegistry
	{
		[operator("create")] public void Init() {}

		// The schema file at `path` (JSONC); its name is the file's absolute path. False with `error` when it cannot be
		// read or is not a schema of the dialect. Loading a file again is free.
		public bool Load(Core::StringView path, Core::String& error);
		// A schema given as text under `name` (a relative $ref in it resolves beside the name, as for a file).
		public bool Add(Core::StringView name, Core::StringView text, Core::String& error);
		// The name a path or a relative reference is kept under: '/' separators, no '.' or '..', ASCII case folded.
		public Core::String KeyOf(Core::StringView path) const;
		// The key of the loaded document `name` names - as given to Add, or a path given to Load in any form; empty when
		// none is loaded.
		public Core::String Find(Core::StringView name) const;
		// The node `ref` ("other.json#/$defs/x", "#/$defs/x") names, seen from the document `base`; `target` is its
		// document's key. Files it names are loaded. False with `error` when it does not resolve.
		public bool Resolve(Core::StringView ref, Core::StringView base, DomNode& node, Core::String& target, Core::String& error);
		// The node at a JSON pointer ("" for the root) of a loaded document.
		public bool At(Core::StringView key, Core::StringView pointer, DomNode& node) const;
		// A loaded pattern, compiled.
		public const Core::Regex* Pattern(Core::StringView source) const;

		private bool Keep(Core::String key, DomDoc* doc, DomNode root, Core::String& error);
		private bool Compile(Core::StringView source, Core::StringView where, Core::String& error);
		private bool CheckSchema(const DomNode& schema, Core::StringView where, Core::String& error);

		private Core::HashMap<Core::String, DomDoc*> docs;
		private Core::HashMap<Core::String, DomNode> roots;
		private Core::HashMap<Core::String, Core::Regex*> patterns;
	}

	// Whether `key` is a keyword of the dialect (or an "x-" annotation).
	bool IsSchemaKeyword(Core::StringView key);
	// Whether a keyword's value is an object of named subschemas (`properties`, `$defs`, `patternProperties`, ...), one
	// subschema (`items`, `not`, `if`, ...) or a list of them (`oneOf`, `prefixItems`, ...): where a schema's
	// subschemas are, for code that walks one.
	bool HoldsNamedSchemas(Core::StringView key);
	bool HoldsOneSchema(Core::StringView key);
	bool HoldsSchemaList(Core::StringView key);
	// The node at a JSON pointer ("/a/0/b~1c") under `root`; false when it is not there.
	bool FollowPointer(const DomNode& root, Core::StringView pointer, DomNode& node);
}
