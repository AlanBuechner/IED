// What a chat has open (docs/04 "What a chat has open"): the chat.resource server binding, through which another plugin
// says a chat uses one of its tabs - the browser's page an AI drives - or no longer does. The list is what is open now:
// kept for this server run only, since after a restart nothing is loaded and a plugin reports its tabs again as they
// load. The chat's topic says when it changes (resources.changed) and the side panel lists them to open.

include "ChatService.ch"

namespace Ai
{
	void ProvideChatResources(IED::PluginContext& context, Chats* chats);
}
