// Chats that go on by themselves after IED restarts (docs/12 "Restart behavior"): one whose turn called a tool that
// restarts the server (IED::ToolResult::restarts: developer mode's ied_relaunch, docs/28), and, as ai.resumeInterrupted
// says, one whose turn the restart cut off. The turn IED starts so is `resumed`, its message IED's; kMostResumes such
// turns in a row that a restart cut off again leave the chat to the owner, so a turn that brings the server down does
// not do it forever.

include "chat.ch"

namespace Ai
{
	// Why `chat`, just loaded, goes on by itself: "relaunch", "interrupted", or "" when it waits for the owner. `policy`
	// is ai.resumeInterrupted's: "agent" (Agent-mode chats), "always" or "never".
	Core::String ResumeReason(const Chat* chat, Core::StringView policy);
	// What the turn that continues it tells the model; `tool` the call that restarted the server, for "relaunch".
	Core::String ResumeText(Core::StringView reason, Core::StringView tool);

	const int64 kMostResumes = 3;
	const int64 kResumeDelayNs = 3000000000;   // the workspace's plugins settle first
}
