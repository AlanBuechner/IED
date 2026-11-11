// Attach to Process (docs/16 "Attaching to a running program"; owner, 2026-11-11: "the profiler should allow us to
// attatch to an already running process including child processes"): a program that profiles but was started without
// IED_PROFILER listens on the local pipe ied-itp-<pid> (Bedrock's Profile/attach.cf, the C++ client's Itp.cpp), so the
// profiler lists those pipes, asks each what it is (hello), and tells the one picked where to connect (attach <target>,
// the workspace's own listener and token): its stream arrives as any program's and becomes a live capture. With its
// child processes, every attachable process under it - however deep, through processes that do not profile (a shell, a
// launcher) - is attached too, and so is each that starts under it later, while it runs (a follower polls).

include "ProfilerService.ch"

namespace Profiler
{
	const char* c_AttachPipePrefix = "ied-itp-";
	const int32 c_AttachAskMs = 1500;
	const int64 c_FollowEveryNs = 1000000000;

	// A process of the machine as the system's snapshot says it.
	class SystemProcess
	{
		public int64 m_Pid;
		public int64 m_ParentPid;
		public Core::String m_Exe;   // its program's file name ("Game.exe")
	}

	// One that IED can attach to: it listens on its pipe.
	class AttachableProcess
	{
		public int64 m_Pid;
		public int64 m_ParentPid;    // the system's parent
		public int64 m_Under;        // the nearest attachable ancestor, 0 for none
		public Core::String m_Exe;
		public Core::String m_App;   // what it calls itself (its hello)
		public Core::String m_State; // idle, attached, or unknown when it did not answer
	}

	// Every process of the machine now (Toolhelp's snapshot).
	Core::Array<SystemProcess*> SystemProcesses();
	// The pids of the processes listening on an attach pipe now.
	Core::Array<int64> AttachPipePids();
	// One line asked of the process's attach pipe and its answer's line; empty when it did not answer.
	Core::String AskAttachPipe(int64 pid, Core::StringView line);
	// The attachable processes with what they say, each with its nearest attachable ancestor.
	Core::Array<AttachableProcess*> AttachableProcesses();
	// Whether `pid` is `ancestor` or under it in `processes` (through any process).
	bool IsUnder(const Core::Array<SystemProcess*>& processes, int64 pid, int64 ancestor);

	// prof.processes {wsId}, prof.attach {wsId, pid, children?}.
	void RegisterAttachMethods(ProfilerPlugin* profiler);
}
