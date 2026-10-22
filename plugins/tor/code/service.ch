// The Tor the plugin runs (docs/29 "Tor"): started when a plugin first asks for its proxy, followed by a watcher thread
// of the plugin's own (the control port, bootstrap progress, the process, the idle stop) and stopped when nothing used
// it for tor.stopAfterSeconds, or with the plugin. Every field is under `lock`; the tor:status topic carries the state.

include "control.ch"
include "torrc.ch"
include "api/plugin.ch"

namespace Tor
{
	enum class TorState { Stopped, Starting, Ready, Failed }

	const char* kStatusTopic = "tor:status";
	const int64 kWatchTickNs = 250000000;
	const int64 kStartLimitNs = 120000000000;          // a start not bootstrapped by then has failed
	const int64 kRetryAfterFailureNs = 5000000000;      // a failed start is tried again on a call after this
	const int64 kNewCircuitSpacingNs = 10000000000;     // Tor takes one NEWNYM every 10 s
	const int64 kAliveCheckNs = 2000000000;
	const int64 kDefaultStopAfterSeconds = 1800;

	const char* StateName(TorState state);

	class TorService
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.context = context; }

		// The watcher thread and the topic.
		public void Start();
		// tor.proxy: starts Tor when it is stopped, failed a while ago or would get another torrc, and answers its state
		// at once - with the proxy once it is ready.
		public Core::String Proxy();
		// tor.newCircuit.
		public Core::String NewCircuit();
		public Core::String Status();
		// tor.stop, and the plugin stopping (`final` also ends the watcher).
		public void Stop(bool final);

		// The torrc a start would write now and the program to run, or `problem` (Tor is not installed).
		private Core::String DesiredTorrc(Core::String& program, Core::String& problem);
		private void Launch(Core::StringView text, Core::StringView program);
		private void Halt(TorState next, Core::StringView why);
		private void Watch();
		private void StepStarting(int64 now);
		private void StepReady(int64 now);
		private Core::String LogTailText();
		private Core::String StatusJson() const;
		private void Publish();

		private IED::PluginContext* context;
		private Threading::Mutex lock;
		private TorState current;
		private int32 progress;
		private Core::String summary;
		private Core::String error;
		private int32 socksPort;
		private Core::String executable;
		private Core::String state;           // the state folder, forward slashes
		private Core::String torrc;           // the running start's
		private Process::Child* process;
		private TorControl control;
		private int64 startedNs;
		private int64 failedNs;
		private int64 usedNs;
		private int64 checkedNs;
		private int64 newCircuitNs;
		private bool stopping;
		private Core::String published;       // the last status the topic got
	}
}
