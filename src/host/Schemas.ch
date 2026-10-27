// HostSchemas: the host API's JSON Schemas (shared/host-api/schemas in the install) in one registry - what plugin
// manifests, solutions and projects are validated against.

include "bedrock.ch"

namespace Ied
{
	class HostSchemas
	{
		[operator("create")] public void Init() {}

		// Loads the schemas from `home`; false with `error` when the install has none (everything then fails validation).
		public bool Load(Core::StringView home, Core::String& error);
		// The registry name of a schema file ("plugin-manifest.v1.schema.json").
		public Core::String Name(const char* file) const;
		// The problems of `value` against a schema file, "<pointer>: <message>" each.
		public Core::Array<Core::String> Check(const char* file, const Serialization::DomNode& value);

		public Serialization::SchemaRegistry* m_Registry;
		private FS::Path m_Folder;
	}

	const char* c_ManifestSchema = "plugin-manifest.v1.schema.json";
	const char* c_SolutionSchema = "solution.v1.schema.json";
	const char* c_ProjectSchema = "project.v1.schema.json";
}
