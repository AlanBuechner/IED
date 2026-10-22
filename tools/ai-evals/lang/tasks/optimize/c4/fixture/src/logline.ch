include "bedrock.ch"

namespace LogStats
{
	// One access log line: "<timestamp> <ip> <method> <path> <status> <latency_ms> <bytes>".
	struct LogLine
	{
		public Core::String timestamp;
		public Core::String ip;
		public Core::String method;
		public Core::String path;
		public int64 status;
		public int64 latencyMs;
		public int64 bytes;
	}

	// The pieces of `text` between separators.
	Core::Array<Core::String> Split(Core::StringView text, int32 separator);
	// False when the line does not have the seven fields, or its numbers do not read.
	bool ParseLine(Core::StringView text, LogLine& line);
}
