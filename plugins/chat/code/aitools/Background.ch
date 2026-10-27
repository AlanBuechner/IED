// Background tasks (docs/04 "Background tasks"): what a chat's AI starts and goes on without waiting for - a command
// (shell_run with background), a project's task (build_run with background) or a sub-agent's chat (agent_run, Agents.ch)
// - each a BackgroundTask of the chat with an id ("bg-3"), and the tools that follow them: background_status,
// background_wait and background_stop. When one ends its chat is told: a running turn at its next step, an idle chat by
// a turn of its own (Chats::BackgroundEnded).

include "ChatService.ch"
include "Work.ch"

namespace Ai
{
	const int64 c_BackgroundKeptBytes = 64 * 1024;       // of a task's output, its end
	const int64 c_BackgroundKept = 20;                  // a chat's tasks kept: the oldest told ones go first
	const int64 c_BackgroundDefaultSeconds = 3600;      // a background command's time when it names none
	const int64 c_BackgroundMostSeconds = 14400;
	const int64 c_BackgroundToldLines = 20;             // of an ended command's output, what the note shows
	const int64 c_BackgroundToldBytes = 4096;
	const int64 c_BackgroundWaitDefaultSeconds = 120;
	const int64 c_BackgroundWaitMostSeconds = 600;
	const int64 c_BackgroundPollNs = 250000000;

	// shell_run and build_run take `background` (their foreground runs otherwise), and background_status,
	// background_wait and background_stop are added, for `chat`'s turn.
	void AddBackgroundTools(ToolRegistry& registry, Chats* chats, Chat* chat);
	// "3 min", "42 s": how long a task ran.
	Core::String TaskDuration(int64 ms);
}
