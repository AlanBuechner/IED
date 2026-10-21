// Chats on disk (docs/04 "Persistence"), under <workspace>/.ied/local/chats/<chatId>/: chat.json the header (title,
// mode, permission, group, task list, queued messages, what was allowed for the chat), turns.jsonl the finished turns
// one per line, with the messages the model was sent, and running.json the turn in flight, written at every step and
// tool boundary. A running.json found at load is a turn a restart interrupted (docs/12 "Restart behavior"): it ends
// "interrupted", a call that was running gets a result saying its effect is unknown, and the chat waits paused.

include "chat.ch"

namespace Ied
{
	FS::Path ChatFolder(const FS::Path& wsRoot, Core::StringView chatId);
	void SaveChatHeader(const FS::Path& wsRoot, const Chat* chat);
	void AppendTurn(const FS::Path& wsRoot, const Chat* chat, const Turn* turn);
	void SaveRunningTurn(const FS::Path& wsRoot, const Chat* chat, const Turn* turn);
	void ForgetRunningTurn(const FS::Path& wsRoot, const Chat* chat);
	// The chats saved for a workspace; `interruptedNote` is what an interrupted call's result tells the model.
	Core::Array<Chat*> LoadChats(const FS::Path& wsRoot, Core::StringView wsId, Core::StringView interruptedNote, Core::Array<Core::String>& problems);
	void DeleteChatFiles(const FS::Path& wsRoot, Core::StringView chatId);
	// Every call of the turn's messages that has no result gets `note` as its result, so the conversation stays one a
	// model accepts (each call answered).
	void CloseOpenCalls(Turn* turn, Core::StringView note);
}
