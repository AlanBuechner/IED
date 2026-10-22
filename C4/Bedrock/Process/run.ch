// Run: a program run to its end with what it printed captured (D-68), for the commands a tool runs and reads.
//     Process::StartInfo info;
//     info.program = Core::String("git");
//     info.arguments.Push(Core::String("rev-parse"));
//     info.arguments.Push(Core::String("HEAD"));
//     Process::Output git = Process::Run(info, 10000);
//     if (git.exitCode == 0) printf("%s", git.output.CStr());

include "child.ch"

namespace Process
{
	struct Output
	{
		public int64 exitCode;         // the child's; kError when it did not start or reading it failed, kTimedOut when it ran too long and was ended
		public Core::String output;    // what it wrote to stdout, and to stderr when `info.errors` is ToOutput
		public Core::String errors;    // what it wrote to stderr
		public Core::String error;     // why exitCode is kError
		public Usage usage;            // what the child used in all: its processor time and peak memory
	}

	// Runs `info` with its stdout and stderr read into the Output (in the order they arrive) and its input from
	// nowhere; `info.input`, `info.output` and `info.terminal` are not read. Ends it and what it started when it has
	// not finished within `timeoutMs`.
	Output Run(const StartInfo& info, int32 timeoutMs = kWaitForever);
}
