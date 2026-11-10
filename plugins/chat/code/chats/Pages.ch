// Pages of a chat's transcript (docs/10 "Chats", docs/04 "What stays in memory"): what clients get of a chat's turns comes
// a page at a time - at most c_PageSegments segments and about c_PageBytes of them, a turn cut where a page ends - so
// neither the server nor a client ever holds a long chat whole for showing it. A finished turn is read from its own
// line of turns.jsonl (indexed when a page is first read, then as the file grows); the running turn is the one in
// memory. The last turn a page read is kept for the next page until c_PageKeptMs pass.
//     TurnPage last = LastPage(state, chat);                                   // what a client opening the chat gets
//     PageBefore(state, chat, Core::StringView("u12"), 40, older, problem);    // what is before segment 40 of u12

include "ChatStore.ch"

namespace Ai
{
	const int64 c_PageSegments = 120;
	const int64 c_PageBytes = 262144;
	const int64 c_PageKeptMs = 60000;

	// The chat's last page: its newest segments.
	TurnPage LastPage(const FS::Path& state, Chat* chat);
	// The page that ends before segment `seg` of turn `turnId` (0: before the turn begins); false with `problem` when
	// the chat has no such turn.
	bool PageBefore(const FS::Path& state, Chat* chat, Core::StringView turnId, int64 seg, TurnPage& page, Core::String& problem);
	// The page that begins at segment `seg` of turn `turnId` (its count or more: with the turn after it).
	bool PageAfter(const FS::Path& state, Chat* chat, Core::StringView turnId, int64 seg, TurnPage& page, Core::String& problem);
	// Its last `count` turns whole - segments and all, the model's messages left out - oldest first: what chat_read and
	// chat_wait tell of a chat.
	Core::Array<Turn*> LastTurnsWhole(const FS::Path& state, Chat* chat, int64 count);
	// The turn a page read let go once c_PageKeptMs passed since it was last used.
	void ForgetPageTurn(Chat* chat, int64 nowMs);
}
