include "logline.ch"

namespace LogStats
{
	struct PathCount
	{
		public Core::String path;
		public int64 count;
	}

	struct IpCount
	{
		public Core::String ip;
		public int64 count;
	}

	struct Stats
	{
		public int64 requests;
		public int64 skipped;
		public int64 bytes;
		public Core::Array<Core::String> ips;           // each address once
		public Core::Array<PathCount> paths;
		public Core::Array<int64> latencies2xx;
		public Core::Array<int64> latencies4xx;
		public Core::Array<int64> latencies5xx;
		public Core::Array<IpCount> serverErrors;       // 5xx responses by address
	}

	void AddLine(Stats& stats, const LogLine& line);
	// Every line of a log; lines that do not parse are counted as skipped.
	Stats Analyze(Core::StringView log);
	// The nearest-rank percentile: the value at rank ceil(percent * count / 100), counting from 1, of the sorted values.
	int64 Percentile(const Core::Array<int64>& values, int64 percent);
	Core::String Report(const Stats& stats);
}
