// Manifest: one plugin's ied-plugin.json (shared/host-api/schemas/plugin-manifest.v1.schema.json, docs/02) read,
// validated against the schema and checked against this host: its API range, its runtime kind, its version.

include "bedrock.ch"

namespace Ied
{
	const char* kHostApiVersion = "1.0.0";   // what an "ied.api" range is checked against
	const char* kManifestFile = "ied-plugin.json";

	enum class PluginSource { BuiltIn, App, Project }

	class Dependency
	{
		public Core::String id;
		public Core::String version;   // a range ("^1.2")
		public bool optional;
	}

	class Manifest
	{
		public Core::String id;
		public Core::String name;
		public Core::String version;
		public Core::String description;
		public Core::String kind;               // runtime.kind: builtin, declarative, js, process
		public Core::String apiRange;           // ied.api
		public Core::Array<Core::String> permissions;
		public Core::Array<Dependency*> dependencies;
		public FS::Path folder;
		public PluginSource source;
		public Serialization::DomDoc* doc;      // keeps `root` and `contributes` alive
		public Serialization::DomNode root;
		public Serialization::DomNode contributes;
		public Core::Array<Core::String> problems;   // why it cannot load; empty when it can
	}

	// The manifest in `folder` from `source`, validated against `schemas` (which holds the manifest schema under
	// `schemaName`); a manifest whose problems are not empty does not load.
	Manifest* ReadManifest(const FS::Path& folder, PluginSource source, Serialization::SchemaRegistry& schemas, Core::StringView schemaName);
	// Whether `version` (a semver "1.2.3") is in `range` ("^1.2", "~1.2.3", "1.2.3", ">=1.2", "*").
	bool VersionInRange(Core::StringView version, Core::StringView range);
	const char* PluginSourceName(PluginSource source);
	// The SHA-256 of a folder's files - their paths and bytes, in name order - as hex: what an approval is for.
	Core::String FolderDigest(const FS::Path& folder);
}
