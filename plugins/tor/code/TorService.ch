// The Tor the plugin runs (docs/29 "Tor"): started when a plugin first asks for its proxy, followed by a watcher thread
// of the plugin's own (the control port, bootstrap progress, the process, the idle stop) and stopped when nothing used
// it for tor.stopAfterSeconds, or with the plugin. Every field is under `lock`; the tor:status topic carries the state.

include "TorControl.ch"
include "Torrc.ch"
include "api/Plugin.ch"

namespace Tor
{
	enum class TorState { Stopped, Starting, Ready, Failed }

	const char* c_StatusTopic = "tor:status";
	const int64 c_WatchTickNs = 250000000;
	const int64 c_StartLimitNs = 120000000000;          // a start not bootstrapped by then has failed
	const int64 c_RetryAfterFailureNs = 5000000000;      // a failed start is tried again on a call after this
	const int64 c_NewCircuitSpacingNs = 10000000000;     // Tor takes one NEWNYM every 10 s
	const int64 c_AliveCheckNs = 2000000000;
	const int64 c_DefaultStopAfterSeconds = 1800;

	const char* StateName(TorState state);

	class TorService
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.m_Context = context; }

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

		private IED::PluginContext* m_Context;
		private Threading::Mutex m_Lock;
		private TorState m_Current;
		private int32 m_Progress;
		private Core::String m_Summary;
		private Core::String m_Error;
		private int32 m_SocksPort;
		private Core::String m_Executable;
		private Core::String m_State;           // the state folder, forward slashes
		private Core::String m_Torrc;           // the running start's
		private Process::Child* m_Process;
		private TorControl m_Control;
		private int64 m_StartedNs;
		private int64 m_FailedNs;
		private int64 m_UsedNs;
		private int64 m_CheckedNs;
		private int64 m_NewCircuitNs;
		private bool m_Stopping;
		private Core::String m_Published;       // the last status the topic got
	}
}
