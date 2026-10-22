// Chats on disk (docs/04 "Persistence"), under the workspace's .ied/local/chats/<chatId>/ (its local state): chat.json the header (title,
// mode, permission, group, task list, queued messages, what was allowed for the chat, the clipboard), turns.jsonl the finished turns
// one per line, with the messages the model was sent, and running.json the turn in flight, written at every step and
// tool boundary. A running.json found at load is a turn a restart interrupted (docs/12 "Restart behavior"): it ends
// "interrupted", a call that was running gets a result saying its effect is unknown, and the chat waits paused.

include "chat.ch"

namespace Ai
{
	// Where the workspace's chats are kept: the chats folder of its local state (docs/07: a folder's is in the app data,
	// never in the folder). The functions below take it as `state`.
	FS::Path LocalStateOf(const IED::Workspace& ws);
	FS::Path ChatFolder(const FS::Path& state, Core::StringView chatId);
	void SaveChatHeader(const FS::Path& state, const Chat* chat);
	void AppendTurn(const FS::Path& state, const Chat* chat, const Turn* turn);
	void SaveRunningTurn(const FS::Path& state, const Chat* chat, const Turn* turn);
	void ForgetRunningTurn(const FS::Path& state, const Chat* chat);
	// The chats saved for a workspace; `interruptedNote` is what an interrupted call's result tells the model.
	Core::Array<Chat*> LoadChats(const FS::Path& state, Core::StringView wsId, Core::StringView interruptedNote, Core::Array<Core::String>& problems);
	void DeleteChatFiles(const FS::Path& state, Core::StringView chatId);
	// Every call of the turn's messages that has no result gets `note` as its result, so the conversation stays one a
	// model accepts (each call answered).
	void CloseOpenCalls(Turn* turn, Core::StringView note);
}
