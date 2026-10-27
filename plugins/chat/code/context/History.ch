// history_search and history_read (docs/04 "Context management", F-AIR-54): what a chat said in the turns its context
// keeps no more. Compression leaves whole earlier turns out of the requests and notes that to the model - "earlier
// parts of this conversation were left out" - so these give it a way back in: the turns stay in the chat in full, on
// disk and in memory, and only stop being sent. search names where a word was said, read gives a turn's messages as
// the model was sent them.

include "aitools/Tool.ch"
include "chats/Chat.ch"

namespace Ai
{
	const int64 c_HistorySearchMost = 8;       // hits one search gives
	const int64 c_HistoryLineBytes = 300;      // of a matched line, and of a message read
	const int64 c_HistoryTurnBytes = 6000;     // of one turn, as history_read gives it
	const int64 c_HistoryReadTurnsMost = 4;    // turns read at once

	// Where a word was said: the turn, the message in it, and the line of the match - its first byte, and which line
	// it is (0-based), so the answer can quote the whole line it came on.
	struct HistoryHit
	{
		public const Turn* m_Turn;
		public int64 m_Message;
		public int64 m_Line;
		public int64 m_Start;
		public int64 m_At;
	}

	// `query`'s words in `turns`: the newest match of each, oldest of those first, at most `most`.
	Core::Array<HistoryHit> SearchTurns(const Core::Array<const Turn*>& turns, Core::StringView query, int64 most);

	// The chat's turns, oldest first, as a search reads them.
	Core::Array<const Turn*> ChatTurns(const Chat* chat);
	// Those of them compression left out of the requests.
	Core::Array<const Turn*> DroppedTurns(const Chat* chat);

	// One turn's messages as the model was sent them, at most `most` bytes.
	Core::String TurnDigest(const Turn* turn, int64 most);

	// history_search and history_read over `chat`'s turns.
	void AddHistoryTools(ToolRegistry& registry, Chat* chat);
}
