// IED's programs found beside each other, and starting the user's ied-server (docs/27 "Starting the server").

include "Control.ch"
include "LogFile.ch"

namespace Launch
{
	const int64 c_ServerStartMs = 15000;
	// ied-server's exit code when another server holds the control pipe: one started at the same moment (a relaunch,
	// another launcher) won the race.
	const int32 c_AnotherServerExit = 3;

	// IED_HEAP_CHECKS=1 in the launcher's environment starts the server with the heap checks of BUG-497 (docs/21 "Heap checks").
	const char* c_HeapChecksVariable = "IED_HEAP_CHECKS";

	// Where the checks' reports go: heap-checks.log in the logs folder.
	FS::Path HeapCheckReportFile();
	// The runtime's checks (C4/docs/runtime/01-memory-and-gc.md): C4_GC_VERIFY_MAPS=<reportFile> C4_GC_POISON=1.
	Core::Array<Core::String> GcCheckEnvironment(Core::StringView reportFile);
	// glibc's checks, found only on Linux, which a server and every process it starts runs under: LD_PRELOAD=
	// libc_malloc_debug.so.0 GLIBC_TUNABLES=glibc.malloc.check=3 MALLOC_PERTURB_=165. Empty elsewhere.
	Core::Array<Core::String> MallocCheckEnvironment();
	// Whether the launcher was asked for the checks: IED_HEAP_CHECKS is set and not 0.
	bool HeapChecksRequested();

	// ied-server.exe (ied-server on Linux) beside this program or in a sibling folder (the build layout); empty when
	// none is.
	FS::Path ServerProgram();
	// ied-desktop.exe, the same way (none on Linux, where the browser shows IED).
	FS::Path DesktopProgram();
	// The web client's folder: the nearest "frontend" above this program that holds login.html; empty when none does.
	FS::Path WebClientFolder();
	// Starts the server in the background, then waits for its pipe: a connection, or a closed stream with `error`. The
	// process goes to `started` when it is given (its exit code tells how it ended). A server that ends because another
	// claimed the pipe meanwhile gives a connection to that one.
	Net::LocalStream StartServer(Core::StringView control, Core::String& error, Process::Child* started = nullptr);
	// A connection to the server on `control` (this user's when empty), starting it when nobody listens (`started`
	// as StartServer's).
	Net::LocalStream Reach(Core::StringView control, Core::String& error, Process::Child* started = nullptr);
	// True when nobody listens on `control`: the server is gone, not merely busy.
	bool ServerGone(Core::StringView control);
	// Waits (at most kServerStartMs) until no one listens on `control`: false when someone still does.
	bool AwaitGone(Core::StringView control);
}
