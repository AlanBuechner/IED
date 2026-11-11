// Chats waiting on each other (docs/04 "Chats waiting on each other"): the replies chats owe each other - chat_send or
// chat_create with require_reply makes the other chat owe one, its next chat_send back pays it - and when a chat may
// suspend. The rule that keeps chats from all sleeping at once is "the newest debt acts": a chat that owes a reply may
// suspend only while it waits on something asked for after the newest reply it owes, so in any set of chats waiting on
// each other the one asked last is awake. Chats' members that change debts are in Replies.cf too (ChatService.ch).

include "chats/Chat.ch"

namespace Ai
{
	// Why a chat may not suspend now.
	enum class SuspendBlock { None, OwesReply, NothingWakes }

	// Whether `chat` may suspend: one that owes a reply only while it waits on something asked for after the newest it
	// owes - a reply it required, a running command or task of its background (they end by themselves), a sub-agent it ran
	// since; one that runs until stopped only while something would wake it.
	SuspendBlock SuspendBlockOf(const Chat* chat);
	// What would wake `chat` once suspended, a line each ("- the reply c2 \"Port Ring.h\" owes this chat"); "" when only the
	// owner's message would.
	Core::String WhatWakes(const Chat* chat);
	// The debt `debtor` owes the chat `creditorId`, or null.
	ReplyOwed* DebtTo(const Chat* debtor, Core::StringView creditorId);
	// Each of `replies` a line: `- c2 "Port Ring.h", asked 5 min ago`.
	Core::String ReplyLines(const Core::Array<ReplyOwed*>& replies, int64 nowMs);
	// The call that replies to `chatId`, as the notes show it: chat_send({"chat": "c1", "text": "..."}).
	Core::String ReplyCall(Core::StringView chatId);
}
