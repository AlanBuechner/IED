// Relaunching IED onto its installed build (docs/27 "Relaunch", src/host/Relaunch.ch): developer mode's ied_relaunch
// after `nuke install-ied` (docs/28).

include "bedrock.ch"

namespace IED
{
	// Asks for a relaunch, permission server.relaunch: a moment later the desktop app watching the server relaunches
	// itself and the server, or else the server starts its program again, with the workspaces the owner opened. Every
	// session reconnects; chats, builds and terminals come back from disk (docs/12 "Restart behavior"). False with
	// `problem` when the plugin may not, or one is already under way. `reason` goes in the server's log.
	bool RelaunchServer(Core::StringView reason, Core::String& problem);
}
