// The SearXNG the plugin runs (docs/29 "SearXNG"): started by the first search under the bundled Python, its
// settings.yml made from the settings each time - a search whose text would differ (a setting changed, Tor came back on
// another port, a new circuit was asked for) restarts it first - and stopped when unused for websearch.stopAfterSeconds.
// With websearch.useTor its engines' requests go through the ied.tor plugin's proxy (tor.proxy), each start's under a
// SOCKS user of its own so Tor gives it circuits of its own. `starting` serializes starts and stops (a search waits on
// it while SearXNG starts); what the status shows is under `fields`, taken after `starting`, before the server's lock.

include "config.ch"
include "results.ch"
include "api/plugin.ch"

namespace WebSearch
{
	enum class SearxState { Stopped, Starting, Ready, Failed, External }

	const char* kStatusTopic = "websearch:status";
	const int64 kStartLimitNs = 90000000000;
	const int64 kTorWaitNs = 90000000000;
	const int64 kTorPollNs = 500000000;
	const int64 kWatchTickNs = 2000000000;
	const int64 kDefaultStopAfterSeconds = 1800;
	const int32 kSearchSlackMs = 15000;   // beyond twice the engines' timeout, for SearXNG and Tor themselves

	const char* SearxStateName(SearxState state);

	// The outcome of the last search, for the status.
	struct LastSearch
	{
		public Core::String query;
		public int64 results;
		public int64 ms;
		public Core::Array<EngineFailure> failed;
		public Core::String error;
		public bool retried;
	}

	class SearxService
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.context = context; }

		// The status topic and the watcher thread.
		public void Start();
		// A search, starting SearXNG (and Tor) first when needed; one that engines refused through this Tor exit - it came
		// back with almost nothing, or half the engines asked refused - is tried once more on new circuits, and one no
		// engine answered at all once more as it is. False with `error`.
		public bool Search(const SearchRequest& request, SearchResults& results, bool& viaTor, Core::String& error);
		// A search's arguments ({query, engines?, count?, page?, time_range?, language?}, as web_search takes them) with
		// the settings' defaults; false with `error` naming what is wrong.
		public bool Prepare(const Serialization::DomNode& args, SearchRequest& request, Core::String& error);
		// websearch.engines's names that SearXNG has; `unknown` gets the others.
		public Core::Array<Core::String> Engines(Core::Array<Core::String>& unknown);
		// Every engine of SearXNG's settings.yml: {engines: [{name, shortcut, categories, off}]}.
		public Core::String CatalogJson();
		// Stops SearXNG and starts it again - with `newCircuit`, on new Tor circuits; false with `error`.
		public bool Restart(bool newCircuit, Core::String& error);
		public Core::String StatusJson();
		// The plugin stops (`final`), or SearXNG is not wanted for now.
		public void Stop(bool final);

		private Core::Array<Core::String> KnownEngines(Core::Array<Core::String>& unknown);
		private bool Ensure(Core::String& base, bool& viaTor, Core::String& error);
		private bool External(Core::StringView instance, Core::String& base, Core::String& error);
		private bool TorProxy(Core::String& proxy, Core::String& error);
		private bool Launch(Core::StringView yaml, Core::String& error);
		private void Halt(SearxState next, Core::StringView why);
		private bool Query(Core::StringView base, const SearchRequest& request, SearchResults& results, Core::String& error);
		private void NewCircuit();
		private void Record(const SearchRequest& request, const SearchResults& results, int64 ms, Core::StringView error, bool retried);
		private void SetState(SearxState next, Core::StringView summary, Core::StringView error);
		private Core::String Folder(const char* setting, const char* bundled) const;
		private Core::String LogTailText() const;
		private void Watch();
		private void Publish();

		private IED::PluginContext* context;
		private Threading::Mutex starting;
		private Threading::Mutex fields;
		// Under `starting`.
		private Process::Child* process;
		private Core::String runningYaml;
		private int64 circuit;
		private bool stopping;
		private bool catalogRead;
		private Core::Array<Core::String> catalog;
		// Under `fields`.
		private SearxState state;
		private Core::String url;
		private Core::String summary;
		private Core::String error;
		private bool viaTorNow;
		private Core::Array<Core::String> engines;
		private Core::Array<Core::String> unknownEngines;
		private LastSearch last;
		private bool searched;
		private int64 usedNs;
		private Core::String published;
	}
}
