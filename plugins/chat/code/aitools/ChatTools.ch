// Chats that talk to each other (docs/04 "Chats talk to each other"): the `chats` tool group - a chat lists its
// workspace's chats, reads one's answers, sends one a message (it starts a turn there, or reaches the running one at its
// next step), starts a helper chat and waits for an answer. A message carries where it came from; a chain of chats
// messaging each other stops at kChatHopsMost.

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
}
