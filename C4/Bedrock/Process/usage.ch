// Usage: what a running process uses, found by the system's id (D-68): its processor time so far and its memory now,
// for a tool that watches a program, its own child or not (a debugger's graphs of the program it debugs).
//     Process::Usage usage;
//     if (Process::GetUsage(child.Id(), usage)) printf("%lld bytes\n", usage.privateBytes);

namespace Process
{
	struct Usage
	{
		public int64 cpuTimeNs;          // processor time in user and kernel mode so far, every thread's
		public int64 workingSetBytes;    // its memory in RAM now
		public int64 privateBytes;       // memory committed to it alone
		public int64 peakPrivateBytes;
	}

	[link("c4_process_usage")] int32 c4_process_usage(uint32 id, Usage* usage);

	// What process `id` uses; false, with `usage` zeroed, when there is no such process or the system will not say.
	bool GetUsage(uint32 id, Usage& usage) { return c4_process_usage(id, &usage) == 0; }
}
