// Compaction (docs/04 "Context management", the summary tier): when a local model's context passes its limit and the
// mechanical tier (Superseded.ch, trimmed results) does not bring it under, everything before a cut is summarized by
// the model itself and stops being sent. Requests then carry, right after the system message, one user message with
// that summary and what IED knows for certain (the tasks, the files changed, the message that started the turn), and
// after it the newest messages as they were. The cut can fall inside the running turn, so a single long turn - an
// Agent turn of hundreds of calls, a chat that runs until stopped - is compacted like a long chat. Nothing is lost:
// the chat keeps every message whole, and history_search / history_read give them back.
//
// The pieces here are the ones that need no model: which messages a request sends, where the cut falls, the transcript
// the summarizer reads in chunks that fit its window, and the digest that stands in when the summarizer fails, so a
// compaction always makes room. The requests themselves are the turn's (RunCompact.cf).

include "chats/Chat.ch"

namespace Ai
{
	const int64 c_CompactTargetPercent = 50;          // of the window: where a compaction aims the context
	const int64 c_CompactTailMostPercent = 20;        // of the window: the most the messages kept whole after the cut hold
	const int64 c_CompactEnoughPercent = 60;          // of the window: under it after the mechanical tier, no summary is made
	const int64 c_CompactLeastPercent = 5;            // of the window: a cut summarizing less is not worth a request
	const int64 c_CompactSummaryMostTokens = 6000;    // what a summary is allowed to take of the window
	const int64 c_CompactChunksMost = 8;              // summarizer requests one compaction makes at most
	const int64 c_CompactOwnerMessageBytes = 6000;    // of the message that started a turn, as the summarizer and the block get it
	const int64 c_CompactMessageBytes = 1500;         // of another user message (a note, a message sent mid-turn)
	const int64 c_CompactReplyBytes = 2500;           // of an assistant's text
	const int64 c_CompactArgsBytes = 400;             // of one call's arguments
	const int64 c_CompactResultBytes = 1200;          // of a tool result
	const int64 c_CompactDigestLineBytes = 300;       // of a line of the digest
	const int64 c_CompactFilesMost = 40;              // files changed the block names

	// A message a request sends: its turn, where it is in the turn's messages, and what it is estimated to count.
	class SentMessage
	{
		public Turn* m_Turn;
		public int64 m_At;
		public ModelMessage* m_Message;
		public int64 m_Tokens;
	}

	// The chat's messages as its requests send them, oldest first: the turns compression left out skipped, a turn a
	// compaction cut from its cut on. Tokens are left 0 for the caller to count.
	Core::Array<SentMessage> SentMessages(const Chat* chat);
	// The messages of `turn` a request sends: from its cut on, all of them when no compaction cut it.
	Core::Array<ModelMessage*> SentPart(const Turn* turn);

	// The most a request's prompt may hold before the context is made smaller: `percent` of `window`, and less when the
	// profile caps the reply at more than the rest (`maxOutput`; 0 for no cap) - a server refuses a prompt that leaves no
	// room for the reply it may be asked for.
	int64 CompactLimit(int64 window, int64 maxOutput, int64 percent);

	// Where a compaction cuts `sent`: the messages before it are summarized, those from it on stay whole. The longest
	// newest run that fits `tailBudget` tokens and starts on a message that is not a tool result (a result never goes
	// without the call it answers); when even the newest such run is larger, that run all the same. 0 when nothing would
	// be left before it to summarize.
	int64 CompactionCut(const Core::Array<SentMessage>& sent, int64 tailBudget);

	// `text` cut to at most `most` bytes on a character's boundary, saying how much was left out.
	Core::String CutText(Core::StringView text, int64 most);

	// One message as the summarizer reads it: who said it, the text cut to its kind's size, and a call's name and
	// arguments.
	Core::String TranscriptEntry(const SentMessage& sent);

	// `sent` [0, to) as transcripts of at most `chunkBytes` each, oldest first; `starts` gets the message each starts at.
	// An entry larger than a chunk is cut to fit one.
	Core::Array<Core::String> TranscriptChunks(const Core::Array<SentMessage>& sent, int64 to, int64 chunkBytes, Core::Array<int64>& starts);

	// What IED can say of `sent` [from, to) with no model, for a summarizer that failed: each turn's message, the calls it
	// made by tool, and the start of its last reply.
	Core::String DigestOf(const Core::Array<SentMessage>& sent, int64 from, int64 to);

	// What IED adds to a summary as it is made: the chat's tasks, the files it changed, and - when `cutTurn` is the
	// chat's current turn and the cut left its first message out - that message, so the turn's request is never only
	// the model's retelling of it.
	Core::String CompactionState(const Chat* chat, const Turn* cutTurn, int64 cutAt);

	// The chat's compaction forgotten: no summary, every turn sent whole again (a revert dropped a turn it covered; the
	// next step compacts again if the rest does not fit).
	void ForgetCompaction(Chat* chat);
}
