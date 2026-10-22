// ProgramUsage (docs/25 "Diagnostic Tools", F-DBG-52): what a debugged program has used, read from its process twice a
// second once the debug adapter names it (DAP's `process` event): when, its share of all processors since the reading
// before, and its memory. The Debugger samples it and publishes each sample; the session keeps the last ones.

include "bedrock.ch"

namespace Ied
{
	const int64 kUsageIntervalMs = 500;
	const int64 kUsageSamplesKept = 1200;

	struct UsageSample
	{
		public int64 ms;                // since the process was named
		public int64 cpuTenths;         // its share of all processors since the sample before, in tenths of a percent
		public int64 privateBytes;
		public int64 workingSetBytes;
	}

	class ProgramUsage
	{
		public uint32 processId;
		public Core::String name;
		public Core::Array<UsageSample> samples;

		// Keeps `sample`, dropping the oldest beyond kUsageSamplesKept.
		public void Add(const UsageSample& sample);
		// {processId, name, samples: [[ms, cpu %, privateBytes, workingSetBytes]]}: the session snapshot's `usage`.
		public Core::String Json() const;
		// {processId, name, sample: [ms, cpu %, privateBytes, workingSetBytes]}: a dbg.usage event.
		public Core::String EventJson(const UsageSample& sample) const;
	}

	// The share of all `processors` a process had: `cpuNs` of processor time over `wallNs`, in tenths of a percent (0
	// to 1000).
	int64 CpuTenths(int64 cpuNs, int64 wallNs, int32 processors);
}
