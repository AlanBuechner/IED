// ValidateJson: the ways a DOM value breaks a schema of a SchemaRegistry (B68), each with the JSON pointer of the part
// that breaks it - the checks and messages of IED's tools/pylib/minischema.py, so the two agree on any document.
//     for (const Serialization::SchemaProblem& problem : Serialization::ValidateJson(schemas, name, "", doc.Root()))
//         printf("%s: %s\n", problem.path.CStr(), problem.message.CStr());

include "schema.ch"

namespace Serialization
{
	struct SchemaProblem
	{
		public Core::String path;      // a JSON pointer into the value ("/contributes/commands/0/id"); "" for the value itself
		public Core::String message;
	}

	// The problems of `value` against the schema at `pointer` ("" for the root) of the registry's document `name` (a
	// path given to Load, or a name given to Add); none when it is valid. A $ref that does not resolve is a problem too.
	Core::Array<SchemaProblem> ValidateJson(SchemaRegistry& registry, Core::StringView name, Core::StringView pointer, const DomNode& value);
	// JSON's equality: numbers by value (1 equals 1.0), objects regardless of member order.
	bool JsonEquals(const DomNode& a, const DomNode& b);
	// "null", "boolean", "integer" (a float with no fraction included), "number", "string", "array" or "object".
	const char* JsonTypeName(const DomNode& value);
}
