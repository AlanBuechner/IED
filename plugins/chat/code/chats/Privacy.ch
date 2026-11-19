// A chat's privacy (docs/04 "Three settings, each its own"; owner, 2026-11-19: "we should add privacy settings for a
// project and individual chats ... each setting its own"): no cloud models, hidden from other chats, no web - each set on
// the chat (chat.set {privacy}), or for every chat of a solution by its ai.privacy.* settings, which a chat adds to and
// never takes away.

include "api/Plugin.ch"
include "Chat.ch"
include "Models.ch"

namespace Ai
{
	// What workspace `wsId`'s solution sets for every chat of it: ai.privacy.noCloudModels, hiddenChats and noWeb.
	ChatPrivacy SolutionPrivacy(const IED::PluginContext* context, Core::StringView wsId);
	// Each setting on where either has it on.
	ChatPrivacy JoinPrivacy(const ChatPrivacy& a, const ChatPrivacy& b);
	// The privacy a chat's turns keep: its own joined with its solution's.
	ChatPrivacy PrivacyOf(const IED::PluginContext* context, const Chat* chat);

	// Whether a chat of `privacy` may run on `profile`: any model, but a cloud one (Claude's are) when it has none.
	bool ModelAllowed(const ModelProfile* profile, const ChatPrivacy& privacy);
	// `candidates` less those ModelAllowed refuses, in order.
	Core::Array<ModelProfile*> AllowedModels(const Core::Array<ModelProfile*>& candidates, const ChatPrivacy& privacy);
	// Why a chat of `privacy` cannot run on `name`, for a tool's error: "" when it can or `models` has no such model.
	Core::String ModelRefusal(const Core::Array<ModelProfile*>& models, Core::StringView name, const ChatPrivacy& privacy);
	// What the turn of a chat whose privacy left none of `candidates` says (a no_model note): which it passed over and what
	// to set up instead.
	Core::String NoAllowedModelNote(const Core::Array<ModelProfile*>& candidates);

	// Whether one of `a` and `b` started the other (chat_create, agent_run) or owes the other a reply: linked chats see
	// each other, hidden or not.
	bool ChatsLinked(const Chat* a, const Chat* b);
	// Whether chat `self`'s AI sees chat `other`: itself, one not hidden (`otherHidden`: by its own privacy or its
	// solution's), or one linked to it.
	bool SeesChat(const Chat* self, const Chat* other, bool otherHidden);
	// The rule refusing the file tools the folder of chat `chatId` on `shelf` (chats, chats-archived, chats-deleted):
	// "fs.*(.ied/local/chats/c1/**)".
	Core::String ChatFolderRule(const char* shelf, Core::StringView chatId);
	// What chat `started`, which `starter` started, takes of it: its own privacy, and who started it.
	void TakePrivacy(Chat* started, const Chat* starter);

	// The system message's word on a chat's privacy (docs/14): a line for each setting it has on; "" for none.
	Core::String PrivacyLines(const ChatPrivacy& privacy);
}
