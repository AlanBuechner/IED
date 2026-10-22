include "bedrock.ch"

namespace LogStats
{
	// One access log line: "<timestamp> <ip> <method> <path> <status> <latency_ms> <bytes>"; the text fields are views
	// of the log, valid while it is.
	struct LogLine
	{
		public Core::StringView timestamp;
		public Core::StringView ip;
		public Core::StringView method;
		public Core::StringView path;
		public int64 status;
		public int64 latencyMs;
		public int64 bytes;
	}

	// The pieces of `text` between separators.
	Core::Array<Core::String> Split(Core::StringView text, int32 separator);
	// False when the line does not have the seven fields, or its numbers do not read.
	bool ParseLine(Core::StringView text, LogLine& line);
}
