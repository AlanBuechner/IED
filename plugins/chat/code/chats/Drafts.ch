// The owner's unsent messages (docs/04 "Unsent messages"): what is typed in a chat's box and not sent - its text and
// mentions - kept by the server, so it survives switching chats and boards, closing the tab, other devices and a
// restart. A chat's is in its chat.json; the workspace's New chat draft, which has no chat yet, in
// .ied/local/chats/draft.json.

include "api/Plugin.ch"
include "Chat.ch"

namespace Ai
{
	// How long an unsent message may be.
	const int64 c_MostDraftBytes = 256 * 1024;

	// {text, mentions} as chat.draft gives it and a snapshot carries it.
	void WriteDraft(IED::JsonWriter& json, const Draft& draft);
	// chat.draft's text and mentions; throws bad_request past c_MostDraftBytes or for a mention chat.send would refuse.
	Draft ReadDraft(const IED::Params& params);
	// The New chat draft of the workspace whose local state is `state`; empty when there is none.
	Draft LoadNewDraft(const FS::Path& state);
	// Kept, or removed when its text is empty.
	void SaveNewDraft(const FS::Path& state, const Draft& draft);
}
