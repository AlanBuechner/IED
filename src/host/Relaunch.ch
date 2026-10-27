// A relaunch onto the installed build (docs/27 "Relaunch", docs/28): a plugin asks for one (IED::RelaunchServer,
// permission server.relaunch). kRelaunchGraceMs later - its answer to whoever asked is out by then - the control
// channel's status says "relaunch": true, and the desktop app that watches the server claims it (`relaunch.claim`) and
// relaunches itself and the server, as its own Relaunch does. Unclaimed kRelaunchClaimMs after that (no app watches),
// or claimed and the server still running kRelaunchClaimedMs later, the server stops and, once stopped, starts its own
// program again: the arguments it was given, the port it has, the workspaces the owner opened, and --relaunched.

include "bedrock.ch"

namespace Ied
{
	enum class RelaunchState { None, Asked, Claimed, Starting }

	class Relaunch
	{
		// The lock is held for each; times are clock_now_ns() in ms.
		// Asks for one at `nowMs`; false when one is under way.
		public bool Ask(Core::StringView reason, int64 nowMs);
		// Whether the status shows it: asked, its grace over, not yet claimed.
		public bool Shown(int64 nowMs) const;
		// The desktop app takes it: true when it was shown.
		public bool Claim(int64 nowMs);
		// True, once, when the server should stop and start itself again now.
		public bool Due(int64 nowMs);
		public bool Starting() const { return m_State == RelaunchState::Starting; }

		public RelaunchState m_State;
		public int64 m_AskedMs;
		public int64 m_ClaimedMs;
		public Core::String m_Reason;
	}

	// The arguments a relaunched server starts with: `given` without --workspace, --port and --relaunched, then --port
	// <port>, --relaunched, and --workspace <path> for each of `paths`.
	Core::Array<Core::String> RelaunchArguments(const Core::Array<Core::String>& given, int32 port, const Core::Array<Core::String>& paths);
	// This program started again with `arguments`, on its own (it outlives this process); false with `problem`.
	bool StartAgain(const Core::Array<Core::String>& arguments, Core::String& problem);

	const int64 c_RelaunchGraceMs = 2000;
	const int64 c_RelaunchClaimMs = 5000;      // the desktop app's watch looks every 2 s
	const int64 c_RelaunchClaimedMs = 30000;
	const int64 c_RelaunchedWaitMs = 20000;    // how long a relaunched server tries for the pipe and the port
}
