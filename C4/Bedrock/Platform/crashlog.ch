// Crash reports: a fatal error - an exception nothing caught, one that left a noexcept function, the collector's -
// ends the program with "C4: fatal: <what>", the thread and the stack that reached it on standard error. A program
// nobody watches the console of (a server, a windowed app) names a crash log to keep the report: it is appended there
// after the time and the process id, and faults nothing handles (an access violation, a stack overflow) are reported
// there too before the system ends the program. A windowed app names a crash reporter to show the report: a program
// started with it as its standard input, which the crashed one waits for before it ends. StackText is the same stack
// for a report of the program's own, such as an exception caught where a thread would have ended.
//     Platform::SetCrashLog(Core::StringView("C:/Users/me/AppData/Local/Game/logs/game.log"));
//     Platform::SetCrashReporter(Platform::ExecutablePath().View(), arguments, error);   // arguments: "--crash-report"
//     catch (...) { log.Write((Core::String("a job failed\n") + Platform::StackText()).View()); }

include "Text/string.ch"
include "Container/array.ch"

namespace Platform
{
	const int64 kStackTextBytes = 16384;
	const int64 kCrashReporterErrorBytes = 1024;

	[link("c4_crash_log")] void c4_crash_log(const char* path) noexcept;
	[link("c4_crash_reporter")] int32 c4_crash_reporter(const char* program, const char** arguments, int32 count, char* error, int64 errorCapacity) noexcept;
	[link("c4_stack_text")] int64 c4_stack_text(char* out, int64 capacity, int32 skip) noexcept;

	// Where fatal reports are also appended (created when missing); empty for standard error only.
	void SetCrashLog(Core::StringView path) { c4_crash_log(Core::String(path).CStr()); }

	// The program each fatal report starts once it is out, with `arguments` and the report's text ("C4: fatal: ...", the
	// thread, the stack) as its standard input; the crashed process ends only after it does. Faults nothing handles are
	// reported too, as with a crash log. An empty program names none. False, with `error` saying why, when there is no
	// such program (a bare name is looked for as Process::Child looks).
	bool SetCrashReporter(Core::StringView program, const Core::Array<Core::String>& arguments, Core::String& error) {
		Core::Array<const char*> texts;
		for (const Core::String& argument : arguments) texts.Push(argument.CStr());
		char[kCrashReporterErrorBytes] why;
		const Core::String name = Core::String(program);
		if (c4_crash_reporter(name.CStr(), texts.Data(), (int32)texts.Count(), &why[0], kCrashReporterErrorBytes) == 0) return true;
		error = Core::String(&why[0]);
		return false;
	}

	// The calling thread's stack, a frame a line ("    at Ns::Function+0x4d (C:/src/a.cf:42)"), from this call
	// outward; in a catch handler, from where the exception was thrown. It reads the PDBs the first time: for reports,
	// not a hot path.
	Core::String StackText() {
		char[kStackTextBytes] text;
		const int64 length = c4_stack_text(&text[0], kStackTextBytes, 0);
		return Core::String(Core::StringView(&text[0], length));
	}
}
