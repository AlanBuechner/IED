// The nuke plugin's package fetch (docs/31 section 6): for each trusted workspace that uses Nuke packages (its
// nuke.toml has [sources] or [packages], or it has a lock: packages/nuke.lock, or nuke.lock from older Nukes),
// `nuke package status --fetch` when it opens or is trusted, then every nuke.packages.fetchMinutes. Nuke fetches each
// develop clone and writes
// .ied/local/readonly/nuke.json again, which the server reads by itself (docs/10 "Read-only folders"): a clone that fell
// behind its remote turns read-only with no Nuke command run by hand. A fetch that fails is told in the workspace's log
// once, and again only after one has worked.

include "api/Plugin.ch"

namespace NukePlugin
{
	class PackageFetcher
	{
		[operator("create")] public void Init(IED::PluginContext* context) { m_Context = context; }

		// Starts the thread that fetches.
		public void Start();
		// Stops it once the fetch it runs, if any, ends.
		public void Stop();

		private void Keep();
		private bool Stopping();
		private bool Due(const IED::Workspace& ws, int64 nowMs);
		private bool HasPackages(const IED::Workspace& ws);
		private Core::String Fetch(const IED::Workspace& ws);
		private void Tell(const IED::Workspace& ws, const Core::String& failure);

		private IED::PluginContext* m_Context;
		private Threading::Mutex m_Lock;                              // guards m_Stopping
		private bool m_Stopping;
		private Core::HashMap<Core::String, int64> m_CheckedMs;      // wsId -> when it was last fetched or found to use no packages
		private Core::HashMap<Core::String, bool> m_Failing;         // wsId -> its last fetch failed (and the log said so)
	}
}
