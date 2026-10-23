// Settings > Environment > Developer's "Rebuild and relaunch" (docs/28): `nuke install-ied` in the source checkout on a
// thread of the plugin's own - one at a time, cancelable - with its output kept (the last kBuildLogLines lines) and
// published line by line on the plugin's topic `ied.developer/state`, beside what the install was made from and how far
// the checkout has moved on since, so the settings page and the status bar follow it from every window.

include "source.ch"

namespace Developer
{
	enum class BuildState { Idle, Running, Succeeded, Failed, Canceled }

	const char* BuildStateName(BuildState state);
	// The lines `text` completes after the unfinished line `pending`; what is left unfinished stays in `pending`.
	Core::Array<Core::String> CompleteLines(Core::String& pending, Core::StringView text);

	class Builds
	{
		[operator("create")] public void Init(IED::PluginContext* context, Core::String source);

		// The topic's snapshot, with the server lock held (the topic pulls it).
		public Core::String SnapshotJson() const;
		// Looks at the checkout again - its HEAD, how far it is past the install - and publishes that; off the lock.
		public void Refresh();
		// Starts `nuke install-ied` in the checkout; false with `problem` while one runs.
		public bool Start(Core::String& problem);
		// Waits for the install that runs, starting one when none does, off the lock: how it ended, its exit code and the
		// last `most` lines of its log.
		public BuildState AwaitInstall(int64& code, Core::String& tail, int64 most);
		public void Cancel();

	private:
		private void Run();
		private void Publish(const Core::Array<Core::String>& lines);
		private void Finish(BuildState state, int64 exitCode);
		private void WriteBuild(IED::JsonWriter& json) const;

		private IED::PluginContext* context;
		private Core::String source;
		private InstallRecord record;
		private bool installed;          // this server runs from an install with a record
		private Core::String head;       // the checkout's HEAD
		private int64 behind;            // commits the checkout has past the install; -1 when unknown
		private BuildState state;
		private int64 startedMs;
		private int64 endedMs;
		private int64 exitCode;
		private Core::Array<Core::String> lines;
		private Threading::Atomic<bool> canceled;
	}

	const int64 kBuildLogLines = 400;
	const int64 kInstallPollNs = 500000000;
	const int64 kBuildMostSeconds = 3600;
}
