// PluginHost (docs/02 "Lifecycle"): finds plugins - built-in ones in the install's plugins/ folder, app ones in
// <app data>/plugins/, and a workspace's project ones: its solution's pins and its .ied/plugins/ folders (docs/26) -
// validates their manifests, lets a stronger source override
// a plugin of the same id, loads an app plugin only once the owner approved its exact content and a project-local one
// only in a trusted workspace (docs/11 "Trust layers"), checks dependencies, keeps which are disabled, and offers the
// declarative contributions of the loaded ones. The "plugins" topic and the plugins.* methods; every member runs with
// the server lock held.

include "Manifest.ch"
include "Languages.ch"
include "web/Rpc.ch"

namespace Ied
{
	enum class PluginState { Loaded, Disabled, Failed, NeedsApproval, Overridden, Untrusted }

	class Plugin
	{
		public Manifest* m_Manifest;
		public PluginState m_State;
		public Core::String m_Digest;        // an app plugin's content digest now
		public Core::String m_OverriddenBy;  // the source of the plugin with this id that won

		// {"id", "name", "version", "kind", "source", "state", "problems", "permissions", "contributes": [point names],
		// "code"?: its code's state (`code`, JSON; empty for none)}
		public Core::String SummaryJson(Core::StringView code) const;
	}

	class PluginHost
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// Reads every plugin folder and the owner's choices again; a broken plugin is kept with its problems.
		public void Load();
		public Plugin* Get(Core::StringView id) const;
		// A loaded plugin's file for /plugins/<id>/<path> (`ws` null) or /w/<wsId>/plugins/<id>/<path> (the plugin loaded
		// for `ws`, a local one included); empty when no such plugin is loaded or it has no such file.
		public FS::Path FileOf(Workspace* ws, Core::StringView id, Core::StringView path);
		public const Core::Array<Plugin*>& All() const { return m_Plugins; }
		// A workspace's project plugins, read now: the solution's pins, then <root>/.ied/plugins/*; untrusted unless the
		// workspace is trusted, and of two with one id the first is the one.
		public Core::Array<Plugin*> LocalFor(Workspace* ws);
		// The language of a file of `ws` (plugins/Languages.ch).
		public Core::String LanguageOf(Workspace* ws, Core::StringView path);
		// The plugins loaded for a workspace (null: none): the global ones, less those a loaded local plugin overrides,
		// and the loaded local ones.
		public Core::Array<Plugin*> LoadedFor(Workspace* ws);
		// The entries of a contribution point the plugins loaded for `ws` give: each plugin's manifest entries, then
		// those its code computed (docs/26 section 3), with the plugin that gave each.
		public Core::Array<Contribution> Entries(Workspace* ws, Core::StringView point);
		// A plugin's code changed state: the plugins topic (or its workspace's) says so.
		public void CodeChanged(CodeInstance* instance);
		// A plugin's code computed new contributions for `wsId` ("": every workspace it is loaded in).
		public void ContributionsChanged(CodeInstance* instance, Core::StringView wsId);

	private:
		private void Discover(const FS::Path& folder, PluginSource source, Core::Array<Manifest*>& found);
		private void Choose(const Core::Array<Manifest*>& found);
		private void CheckDependencies();
		private void ReadChoices();
		private bool SaveChoices() const;
		private void Changed();
		private Core::String Snapshot() const;
		private Core::String Summary(Plugin* plugin, Workspace* ws) const;
		private Core::String ContributesJson(Plugin* plugin, Workspace* ws) const;
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String SetEnabledMethod(Call& call, const Params& params);
		private Core::String ApproveMethod(Call& call, const Params& params);
		private Core::String ContributionsMethod(Call& call, const Params& params);
		private Core::String ReloadMethod(Call& call, const Params& params);

		private Server* m_Server;
		private Core::Array<Plugin*> m_Plugins;
		private Core::HashMap<Core::String, bool> m_Disabled;
		private Core::HashMap<Core::String, Core::String> m_Approved;   // app plugin id -> the digest the owner approved
	}

	// One entry of a contribution point and the plugin that gave it.
	struct Contribution
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Plugin* from, Serialization::DomNode given) {
			m_Plugin = from;
			m_Entry = given;
		}
		[operator("create")] public void Init(Plugin* from, Serialization::DomNode given, bool byCode) {
			m_Plugin = from;
			m_Entry = given;
			m_Computed = byCode;
		}

		public Plugin* m_Plugin;
		public Serialization::DomNode m_Entry;
		public bool m_Computed;   // the plugin's code gave it (PluginContext::Contribute), not its manifest
	}

	const char* PluginStateName(PluginState state);
}
