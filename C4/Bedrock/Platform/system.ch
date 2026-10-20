// The machine and this process (D-63): processors and caches, memory, the executable's path and environment
// variables. Text is UTF-8.
//     Platform::ProcessorInfo cpu = Platform::GetProcessorInfo();
//     Core::String home = Platform::GetEnvironmentVariable("USERPROFILE");

include "../Text/string.ch"
include "../FS/path.ch"

namespace Platform
{
	struct ProcessorInfo
	{
		public int32 logicalProcessors;   // hardware threads the system runs
		public int32 cores;               // physical cores
		public int32 packages;            // processor sockets
		public int32 cacheLineBytes;      // the L1 data cache's line (64 when the system does not say)
		public int64 l1DataBytes;         // one core's L1 data cache; 0 when there is none
		public int64 l2Bytes;             // one L2 cache
		public int64 l3Bytes;             // one L3 cache
	}

	struct MemoryStatus
	{
		public int64 totalPhysicalBytes;
		public int64 availablePhysicalBytes;
		public int64 commitLimitBytes;        // physical memory plus the page files
		public int64 availableCommitBytes;
		public int64 totalVirtualBytes;       // this process's user address space
		public int64 availableVirtualBytes;   // the part of it nothing has reserved
	}

	struct ProcessMemory
	{
		public int64 workingSetBytes;   // this process's memory in RAM now
		public int64 peakWorkingSetBytes;
		public int64 privateBytes;      // memory committed to this process alone
		public int64 peakPrivateBytes;
	}

	ProcessorInfo GetProcessorInfo();
	MemoryStatus GetMemoryStatus();
	ProcessMemory GetProcessMemory();
	[link("c4_system_process_id")] uint64 ProcessId();
	// The running program's file; empty when the system cannot say.
	FS::Path ExecutablePath();

	// Environment variables of this process, which the processes it starts inherit. A name is not empty and has no
	// '='. Setting one changes the system's block, not a copy the C runtime's getenv may hold.
	// false when `name` is not set (a variable set to "" is set).
	bool TryGetEnvironmentVariable(const char* name, Core::String& value);
	// Empty when `name` is not set.
	Core::String GetEnvironmentVariable(const char* name);
	bool SetEnvironmentVariable(const char* name, const char* value);
	// True also when it was not set; false for an invalid name.
	bool RemoveEnvironmentVariable(const char* name);

	// `count` bytes from the system's cryptographically secure generator (keys, nonces, masks); false when it fails.
	[link("c4_system_random_bytes")] int32 c4_system_random_bytes(void* out, int64 count);
	bool RandomBytes(void* out, int64 count) { return c4_system_random_bytes(out, count) == 0; }
}
