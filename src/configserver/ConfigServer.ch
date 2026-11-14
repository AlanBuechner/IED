// ConfigServer (docs/35): the master given --config-root serves that folder to every IED of the owner's -
//     settings.json            the served settings, edited by hand or replaced through the API
//     published/<plugin>.json  what server plugins published (IED::Shared), merged after settings.json by merge kind
//     plugins/<id>/            client plugins: clients load them in the shared scope
//     skills/<name>/           skills: clients find them after the app's
//     server-plugins/<id>/     C4 plugins this server runs (the plugin host's Server source), never sent
//     access.json              the tokens' hashes (ConfigAccess.ch)
// It watches the folder: a change of anything served makes a new version, and the requests waiting for one answer.
// Deploys stage a package's files in <kind>/.staging/<id>/ and commit them by renaming the folder into place. Its own
// locks only, never the server lock (in the order: writes, scans, state); the server lock may be held around
// CatalogChanged.

include "ConfigAccess.ch"
include "ConfigPackages.ch"
include "fs/Watching.ch"
include "settings/Catalog.ch"

namespace Ied
{
	// A package the state lists.
	class ServedPackage
	{
		public Core::String m_Id;
		public Core::String m_Version;   // a plugin manifest's version; empty for a skill
		public Core::String m_Hash;
	}

	// What a deploy or a removal came to: an HTTP status and a JSON body.
	class ConfigOutcome
	{
		public int32 m_Status;
		public Core::String m_Json;
	}

	class ConfigServer
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server, const FS::Path& root);

		// Makes the folder, reads access.json and what is served, and starts watching; false with `error` when the
		// folder cannot be used.
		public bool Start(Core::String& error);
		// The server stops: requests waiting for a version answer now.
		public void Stop();
		public const FS::Path& Root() const { return m_Root; }

		// Reads what is served again; requests waiting for a version wake when it changed.
		public void Rescan();
		// The served settings' merge kinds, from the settings catalog (the server lock may be held).
		public void CatalogChanged(const Core::HashMap<Core::String, MergeKind>& kinds);
		// The state: {version, settings, plugins: [{id, version, hash}], skills: [{name, hash}]}. With `since` equal to
		// the version, it waits at most `waitMs` for another (or for the server to stop).
		public Core::String State(Core::StringView since, int64 waitMs);
		public Core::String Version();
		// The role `token` holds, and its name.
		public ConfigRole Check(Core::StringView token, Core::String& name);

		// A package's folder: `kind` is plugins, skills or server-plugins; empty for another kind.
		public FS::Path KindFolder(Core::StringView kind) const;
		// One file of a package being deployed, in <kind>/.staging/<id>/<path>.
		public ConfigOutcome Stage(Core::StringView kind, Core::StringView id, Core::StringView path, Core::StringView bytes);
		// Drops what is staged for <kind>/<id>.
		public ConfigOutcome ClearStage(Core::StringView kind, Core::StringView id);
		// The staged files - exactly those `listJson` ([{path, hash}]) names, with those hashes - become the package;
		// `who` (the token's name) goes to the security log.
		public ConfigOutcome Commit(Core::StringView kind, Core::StringView id, Core::StringView listJson, Core::StringView who);
		// Removes a package (a server plugin's published values with it).
		public ConfigOutcome Remove(Core::StringView kind, Core::StringView id, Core::StringView who);
		// settings.json replaced by `text`, which must read as a JSON(C) object.
		public ConfigOutcome ReplaceSettings(Core::StringView text, Core::StringView who);
		// settings.json as written ("{}" when there is none).
		public Core::String SettingsText();

		// IED::Shared: `key` = `valueJson` published by `plugin`, kept in published/<plugin>.json.
		public bool Publish(Core::StringView plugin, Core::StringView key, Core::StringView valueJson, Core::String& problem);
		public bool Unpublish(Core::StringView plugin, Core::StringView key, Core::String& problem);
		// {key: value} of what `plugin` published ("{}" for nothing).
		public Core::String PublishedJson(Core::StringView plugin);
		// Why the served settings could not be read last ("" when they could).
		public Core::String SettingsProblem();

	private:
		private void Watched(const ChangeBatch& batch);
		private void LoadAccess();
		private void Record(const char* event, Core::StringView fields);
		private bool WritePublished(Core::StringView plugin, Core::StringView json, Core::String& problem);
		private Core::String ServedSettings(Core::String& problem);
		private void ReloadServerPlugins();

		private Server* m_Server;
		private FS::Path m_Root;
		private FolderWatch m_Watch;
		private Threading::Mutex m_WriteLock;   // deploys, removals, publishes: one at a time
		private Threading::Mutex m_ScanLock;    // one scan at a time
		// Under m_Lock:
		private Threading::Mutex m_Lock;
		private Threading::ConditionVariable m_Changed;
		private bool m_Stopping;
		private Core::String m_Version;
		private Core::String m_StateJson;
		private Core::String m_SettingsJson;      // the last served settings that read
		private Core::String m_SettingsProblem;
		private ConfigAccess m_Access;
		private Core::HashMap<Core::String, MergeKind> m_Kinds;
	}

	// The kinds of package a config server keeps.
	const char* c_PluginsKind = "plugins";
	const char* c_SkillsKind = "skills";
	const char* c_ServerPluginsKind = "server-plugins";
	// The longest a state request waits for a new version.
	const int64 c_LongestStateWaitMs = 60000;

	// The served settings: `base` (settings.json's members) with each published file's members merged over them in
	// order, each by its key's merge kind in `kinds` (replace for a key no plugin here declares); a JSON object.
	Core::String MergePublished(Core::StringView base, const Core::Array<Core::String>& published, const Core::HashMap<Core::String, MergeKind>& kinds);
	// The version of a state: one digest of the served settings and every package's id and hash.
	Core::String StateVersion(Core::StringView settings, const Core::Array<ServedPackage*>& plugins, const Core::Array<ServedPackage*>& skills);
}
