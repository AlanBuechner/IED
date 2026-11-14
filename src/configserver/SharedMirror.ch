// SharedMirror (docs/35 "The client"): the master follows the config server the app settings name (configServer.url,
// configServer.token, configServer.certificate) with a waiting state request on a thread of its own, and mirrors what
// it serves into <app data>/shared/ - settings.json (the Shared settings tier, docs/07), plugins/<id>/ (the shared plugin
// scope, docs/26) and skills/<name>/ (shared skills) - each package fetched into shared/.staging/, checked file by file
// and as a whole, then renamed into place, so no reader sees half of one. Every server reads the mirror as it reads the
// app tier (host/Appdata.ch watches it). A config server that cannot be reached leaves the mirror as it was, and IED
// never waits for one: only the status says so - the `server` topic's `configServer`, `configServer.changed` when it
// changes, a notification when following stops, and a line in the IED output.

include "ConfigPackages.ch"

namespace Ied
{
	// What the app settings say to follow, read with the server lock held.
	class MirrorTarget
	{
		public Core::String m_Url;        // ends with '/'; empty: follow nothing
		public Core::String m_Token;
		public Core::String m_Problem;    // why the settings cannot be followed (no token, a bad URL)
		public Net::TlsClientOptions m_Tls;
	}

	class SharedMirror
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.m_Server = server; }

		// Starts following on a thread of its own (the master, once it listens).
		public void Start();
		// The settings changed: the thread looks at them again now rather than at its next turn. The server lock may be
		// held.
		public void Wake();
		// {state, url, error?, version?, syncedMs?, plugins, skills}: state off, connecting, following, unreachable,
		// refused (the token) or failed (what it served did not mirror). Its own lock.
		public Core::String StatusJson();

	private:
		private void Run();
		private MirrorTarget Target();
		private bool Follow(const MirrorTarget& target, Core::String& since);
		private bool Mirror(const MirrorTarget& target, const Serialization::DomNode& state, Core::String& problem);
		private bool MirrorKind(const MirrorTarget& target, Core::StringView kind, const Serialization::DomNode& listed, const char* idField, Core::String& problem);
		private bool Fetch(const MirrorTarget& target, Core::StringView kind, Core::StringView id, Core::StringView hash, const FS::Path& into, Core::String& problem);
		private bool WriteSettings(const MirrorTarget& target, const Serialization::DomNode& settings, Core::String& problem);
		private void SetStatus(Core::StringView state, Core::StringView url, Core::StringView error);
		private void Synced(Core::StringView version, int64 plugins, int64 skills);
		private void Sleep(int64 ms);
		private void Record(const char* event, Core::StringView fields);
		private FS::Path SharedFolder() const;

		private Server* m_Server;
		private Threading::Mutex m_Lock;
		private Threading::ConditionVariable m_Wake;
		private bool m_Woken;
		private Core::String m_State;
		private Core::String m_Url;
		private Core::String m_Error;
		private Core::String m_Version;
		private int64 m_SyncedMs;
		private int64 m_Plugins;
		private int64 m_Skills;
	}

	// How long a state request waits on the config server, and how long the master waits for its answer past that.
	const int64 c_MirrorWaitS = 25;
	const int32 c_MirrorSlackMs = 15000;
	// The longest pause between tries at a config server that cannot be reached.
	const int64 c_MirrorMostBackoffMs = 30000;

	// The base URL of `url` ending with '/', or empty with `problem` when it is not http:// or https://.
	Core::String MirrorBaseUrl(Core::StringView url, Core::String& problem);
	// The token a configServer.token value (JSON) names: {"env": "VAR"} the variable's value; {"secret": "<name>"} the
	// secret store's, which is not built yet (docs/11 "Secrets"), so it is a problem saying so. Empty with `problem`.
	Core::String MirrorToken(Core::StringView valueJson, Core::String& problem);
}
