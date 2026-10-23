// LogFile: a log in IED's logs folder (%LOCALAPPDATA%\IED\logs, docs/21), kept across runs so a program that stopped
// can be asked why. One line each, "<date> <time> [<pid>] <text>", appended and closed at once, so the last line before
// a crash is on disk; opening moves a log past kLogFileBytes to <name>.1.log.

include "bedrock.ch"

namespace Launch
{
	const int64 kLogFileBytes = 4194304;

	class LogFile
	{
		[operator("create")] public void Init() {}

		// Logs to `file` from now on, its folder made and an old log moved aside first; false when it cannot be written.
		public bool Open(Core::StringView file);
		public void Write(Core::StringView line);
		public Core::String File() const { return file; }

		private Core::String file;
		private Threading::Mutex lock;
	}

	// %LOCALAPPDATA%\IED\logs.
	FS::Path LogsFolder();
	// "2026-10-22 23:26:02.123 [4242] " for `unixNs` and process `pid`.
	Core::String LogStamp(int64 unixNs, uint64 pid);
	// The file a log moves to when it grows past kLogFileBytes: desktop.log's desktop.1.log.
	Core::String RotatedLogFile(Core::StringView file);
	// How a program ended, from its exit code: "code 1", or "exception 0xC0000005" for a code that is a Windows
	// exception's (a crash).
	Core::String ExitCodeText(int64 code);
}
