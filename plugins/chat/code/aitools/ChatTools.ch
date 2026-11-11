// Chats that talk to each other (docs/04 "Chats talk to each other"): the `chats` tool group - a chat lists its
// workspace's chats, reads one's answers, sends one a message (it starts a turn there, or reaches the running one at its
// next step), starts a helper chat and waits for an answer. A message carries where it came from; a chain of chats
// messaging each other stops at kChatHopsMost. And chat_title, outside the group: a chat names itself (docs/04 "Chat names").

include "ChatService.ch"

namespace Ai
{
	const int64 c_ChatHopsMost = 3;          // chats in a row that messaged each other
	const int64 c_ChatReadTurnsMost = 5;
	const int64 c_ChatWaitMostSeconds = 600;
	const int64 c_ChatWaitDefaultSeconds = 120;

	// The group's tools for a turn of `chat`, run on its thread with the lock taken for what they read and change.
	void AddChatTools(ToolRegistry& registry, Chats* chats, Chat* chat);
	// A turn's answer: its text segments, joined.
	Core::String AnswerOf(const Turn* turn);
	// Where a message `hops` long in a chain of chats messaging each other stands, as the model is told it with the
	// message or the send: "message 2 of at most 3 in a row between chats ...".
	Core::String ChainNote(int64 hops);
	// Whether a message `hops` long can still be answered with chat_send.
	inline bool CanAnswer(int64 hops) { return hops < c_ChatHopsMost; }
	// What the sender of a message `hops` long is told of it (chat_send, chat_create): ChainNote, and when it is the chain's last,
	// that the other chat cannot message this one back - so a hand-off that asks to be told when it is done is not left waiting.
	Core::String SentNote(int64 hops);
	// Whether a wait tool of `chat`'s running turn stops now: a message came for the turn - the owner's or another chat's
	// (docs/04 "Chats waiting on each other"). What it waited on goes on. Inside the host lock.
	bool WaitInterrupted(const Chat* chat);
}
