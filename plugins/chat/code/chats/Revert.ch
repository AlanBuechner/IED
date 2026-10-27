// Reverting a chat (docs/04 "Reverting a chat", docs/10 "Chats"): chat.revert {chatId, turnId} takes that turn and every
// later one out of the chat - the owner's Edit or Resend of one of their messages - and loses none of them: their lines
// of turns.jsonl move whole to reverted/<ms>-<turnId>.jsonl in the chat's folder. What followed from them goes with them:
// context.json keeps what compression shows of the turns kept only, the context meter is made again from them, what the
// model server's prefix cache was known to hold is forgotten, and a Claude Code session that holds a dropped turn is
// left, so the next turn starts a new one told what the chat kept. Files the dropped turns changed are not changed back.

include "Chat.ch"

namespace Ai
{
	const int64 c_RevertStopWaitNs = 30000000000;   // a running turn stopped for a revert ends within this

	// Moves the lines of the chat's turns.jsonl from turn `turnId`'s on into a new file of the chat's reverted/ folder,
	// the lines before it left as they were; `moved` that file's path in the chat's folder ("reverted/<ms>-<turnId>.jsonl").
	// False with `problem` when turns.jsonl has no line for the turn or a file cannot be written. The moved lines are
	// written first, so a stop in between leaves them in both files, never in neither.
	bool MoveRevertedTurns(const FS::Path& state, Core::StringView chatId, Core::StringView turnId, int64 nowMs, Core::String& moved, Core::String& problem);
}
