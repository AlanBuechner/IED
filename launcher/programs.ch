// IED's programs found beside each other, and starting the user's ied-server (docs/27 "Starting the server").

include "control.ch"
include "logfile.ch"

namespace Launch
{
	const int64 kServerStartMs = 15000;

	// ied-server.exe beside this program or in a sibling folder (the build layout); empty when none is.
	FS::Path ServerProgram();
	// ied-desktop.exe, the same way.
	FS::Path DesktopProgram();
	// The web client's folder: the nearest "frontend" above this program that holds login.html; empty when none does.
	FS::Path WebClientFolder();
	// Starts the server in the background, then waits for its pipe: a connection, or a closed stream with `error`. The
	// process goes to `started` when it is given (its exit code tells how it ended).
	Net::LocalStream StartServer(Core::StringView control, Core::String& error, Process::Child* started = nullptr);
	// A connection to the server on `control` (this user's when empty), starting it when nobody listens (`started`
	// as StartServer's).
	Net::LocalStream Reach(Core::StringView control, Core::String& error, Process::Child* started = nullptr);
	// True when nobody listens on `control`: the server is gone, not merely busy.
	bool ServerGone(Core::StringView control);
	// Waits (at most kServerStartMs) until no one listens on `control`: false when someone still does.
	bool AwaitGone(Core::StringView control);
}
