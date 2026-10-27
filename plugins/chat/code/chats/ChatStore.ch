// Chats on disk (docs/04 "Persistence"), under the workspace's .ied/local/chats/<chatId>/ (its local state): chat.json the header (title,
// mode, permission, group, task list, queued messages, what was allowed for the chat, the clipboard), turns.jsonl the finished turns
// one per line, with the messages the model was sent, and running.json the turn in flight, written at every step and
// tool boundary. A running.json found at load is a turn a restart interrupted (docs/12 "Restart behavior"): it ends
// "interrupted", a call that was running gets a result saying its effect is unknown, and the chat waits paused.
// context.json is what compression shows of the messages in place of their content (docs/04 "Context management"), so a
// chat loaded again sends exactly what it sent before; the turns' files always keep the messages whole.

include "Chat.ch"

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
	// context.json: {messages: [{turn, at, shown?, imagesOut?}]}, every message compression changed what requests send of.
	void SaveContextView(const FS::Path& state, const Chat* chat);
	// The chats saved for a workspace; `interruptedNote` is what an interrupted call's result tells the model.
	Core::Array<Chat*> LoadChats(const FS::Path& state, Core::StringView wsId, Core::StringView interruptedNote, Core::Array<Core::String>& problems);
	// One saved chat, or null with a problem.
	Chat* LoadChat(const FS::Path& state, Core::StringView wsId, Core::StringView chatId, Core::StringView interruptedNote, Core::Array<Core::String>& problems);

	// A deleted chat (docs/04 "Deleting a chat"): its folder moved to the local state's chats-deleted/<chatId>/, with
	// deleted.json saying when, kept there whole until it is restored or purged.
	class DeletedChat
	{
		public Core::String m_ChatId;
		public Core::String m_WsId;
		public Core::String m_Title;
		public int64 m_UpdatedMs;
		public int64 m_DeletedMs;
		public int64 m_TurnCount;
	}

	FS::Path DeletedChatFolder(const FS::Path& state, Core::StringView chatId);
	// Moves the chat's folder among the deleted ones, deleted at `nowMs`; false with `problem` when it cannot.
	bool MoveToDeleted(const FS::Path& state, Core::StringView chatId, int64 nowMs, Core::String& problem);
	// Moves a deleted chat's folder back among the chats; false with `problem` when it cannot.
	bool MoveFromDeleted(const FS::Path& state, Core::StringView chatId, Core::String& problem);
	Core::Array<DeletedChat*> LoadDeletedChats(const FS::Path& state, Core::StringView wsId, Core::Array<Core::String>& problems);
	void PurgeDeletedChat(const FS::Path& state, Core::StringView chatId);
	// Every call of the turn's messages that has no result gets `note` as its result, so the conversation stays one a
	// model accepts (each call answered).
	void CloseOpenCalls(Turn* turn, Core::StringView note);
}
