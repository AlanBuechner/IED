include "logline.ch"

namespace LogStats
{
	struct Stats
	{
		public int64 requests;
		public int64 skipped;
		public int64 bytes;
		public Core::HashSet<Core::String> ips;
		public Core::HashMap<Core::String, int64> paths;
		public Core::Array<int64> latencies2xx;
		public Core::Array<int64> latencies4xx;
		public Core::Array<int64> latencies5xx;
		public Core::HashMap<Core::String, int64> serverErrors;   // 5xx responses by address
	}

	void AddLine(Stats& stats, const LogLine& line);
	// Every line of a log; lines that do not parse are counted as skipped.
	Stats Analyze(Core::StringView log);
	// The nearest-rank percentile: the value at rank ceil(percent * count / 100), counting from 1, of the sorted values.
	int64 Percentile(const Core::Array<int64>& values, int64 percent);
	// The same, of values already sorted.
	int64 PercentileOfSorted(const Core::Array<int64>& sorted, int64 percent);
	Core::String Report(const Stats& stats);
}
