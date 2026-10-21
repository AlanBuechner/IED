// SettingsCatalog (docs/07 "Where settings are declared"): every setting the loaded plugins declare - its schema, its
// default, the tiers it may be set in and how tiers merge it - and the checks a value must pass.

include "plugins/host.ch"

namespace Ied
{
	enum class MergeKind { Replace, Merge, Append }

	const char*[3] kSettingTiers = { "app", "solution", "project" };

	class SettingInfo
	{
		public Core::String key;
		public Core::String plugin;
		public Core::String schemaName;     // the plugin's settings schema in the catalog's registry
		public Core::String pointer;        // "/properties/<key>" in it
		public Core::String defaultJson;    // "null" when it has none
		public Core::Array<Core::String> tiers;
		public MergeKind merge;
		public Core::String page;
	}

	class SettingsCatalog
	{
		[operator("create")] public void Init() {}

		// The settings of the loaded `plugins`; why a plugin's settings could not be read goes to `problems`.
		public void Build(const Core::Array<Plugin*>& plugins, Core::Array<Core::String>& problems);
		public SettingInfo* Get(Core::StringView key) const;
		public const Core::Array<SettingInfo*>& All() const { return order; }
		public bool AllowedIn(const SettingInfo* setting, Core::StringView tier) const;
		// What is wrong with `value` for `setting`: "<pointer>: <message>" each.
		public Core::Array<Core::String> Check(const SettingInfo* setting, const Serialization::DomNode& value);
		// {settings: {<key>: {plugin, schema, default, tiers, merge, page?}}}
		public Core::String SchemaJson();

		private bool Declare(Plugin* plugin, Core::String& problem);

		private Serialization::SchemaRegistry* registry;
		private Core::HashMap<Core::String, SettingInfo*> settings;
		private Core::Array<SettingInfo*> order;
	}

	const char* MergeKindName(MergeKind kind);
	// "/properties/<key>" with the key's '~' and '/' escaped for a JSON pointer.
	Core::String PropertyPointer(Core::StringView key);
}
