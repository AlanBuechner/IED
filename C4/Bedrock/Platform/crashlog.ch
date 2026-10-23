// Crash reports: a fatal error - an exception nothing caught, one that left a noexcept function, the collector's -
// ends the program with "C4: fatal: <what>", the thread and the stack that reached it on standard error. A program
// nobody watches the console of (a server, a windowed app) names a crash log to keep the report: it is appended there
// after the time and the process id, and faults nothing handles (an access violation, a stack overflow) are reported
// there too before the system ends the program. StackText is the same stack for a report of the program's own, such
// as an exception caught where a thread would have ended.
//     Platform::SetCrashLog(Core::StringView("C:/Users/me/AppData/Local/Game/logs/game.log"));
//     catch (...) { log.Write((Core::String("a job failed\n") + Platform::StackText()).View()); }

include "Text/string.ch"

namespace Platform
{
	const int64 kStackTextBytes = 16384;

	[link("c4_crash_log")] void c4_crash_log(const char* path) noexcept;
	[link("c4_stack_text")] int64 c4_stack_text(char* out, int64 capacity, int32 skip) noexcept;

	// Where fatal reports are also appended (created when missing); empty for standard error only.
	void SetCrashLog(Core::StringView path) { c4_crash_log(Core::String(path).CStr()); }

	// The calling thread's stack, a frame a line ("    at Ns::Function+0x4d (C:/src/a.cf:42)"), from this call
	// outward; in a catch handler, from where the exception was thrown. It reads the PDBs the first time: for reports,
	// not a hot path.
	Core::String StackText() {
		char[kStackTextBytes] text;
		const int64 length = c4_stack_text(&text[0], kStackTextBytes, 0);
		return Core::String(Core::StringView(&text[0], length));
	}
}
