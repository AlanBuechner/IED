// Manifest: one plugin's ied-plugin.json (shared/host-api/schemas/plugin-manifest.v1.schema.json, docs/02) read,
// validated against the schema and checked against this host: its API range, its runtime kind, its version.

include "bedrock.ch"

namespace Ied
{
	const char* c_HostApiVersion = "1.0.0";   // what an "ied.api" range is checked against
	const char* c_ManifestFile = "ied-plugin.json";

	enum class PluginSource { BuiltIn, App, Project }

	class Dependency
	{
		public Core::String m_Id;
		public Core::String m_Version;   // a range ("^1.2")
		public bool m_Optional;
	}

	class Manifest
	{
		public Core::String m_Id;
		public Core::String m_Name;
		public Core::String m_Version;
		public Core::String m_Description;
		public Core::String m_Kind;               // runtime.kind: builtin, declarative, c4
		public Core::String m_Ui;                 // runtime.ui: its browser module's path in its folder; empty when none
		public Core::String m_Entry;              // runtime.entry: a c4 plugin's entry function ("Markup::Activate")
		public Core::Array<Core::String> m_Sources;      // runtime.sources: its code's folders and files (default "code")
		public Core::Array<Core::String> m_Activation;   // when its code starts (docs/26 section 2); empty: at load
		public Core::Array<Core::String> m_Servers;      // runtime.servers: where its code runs, master and/or workspace (docs/26 "Servers")
		public Core::String m_ApiRange;           // ied.api
		public Core::Array<Core::String> m_Permissions;
		public Core::Array<Dependency*> m_Dependencies;
		public FS::Path m_Folder;
		public PluginSource m_Source;
		public Serialization::DomDoc* m_Doc;      // keeps `root` and `contributes` alive
		public Serialization::DomNode m_Root;
		public Serialization::DomNode m_Contributes;
		public Core::Array<Core::String> m_Problems;   // why it cannot load; empty when it can
	}

	// The manifest in `folder` from `source`, validated against `schemas` (which holds the manifest schema under
	// `schemaName`); a manifest whose problems are not empty does not load.
	Manifest* ReadManifest(const FS::Path& folder, PluginSource source, Serialization::SchemaRegistry& schemas, Core::StringView schemaName);
	// Whether `version` (a semver "1.2.3") is in `range` ("^1.2", "~1.2.3", "1.2.3", ">=1.2", "*").
	bool VersionInRange(Core::StringView version, Core::StringView range);
	const char* PluginSourceName(PluginSource source);
	// Whether `path` names a file in `folder` by a relative path a URL can carry as it is (letters, digits, '.', '_', '-',
	// '/'; no '..').
	bool IsUiPath(const FS::Path& folder, Core::StringView path);
	// The SHA-256 of a folder's files - their paths and bytes, in name order - as hex: what an approval is for.
	Core::String FolderDigest(const FS::Path& folder);
}
