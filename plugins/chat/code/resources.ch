// What a chat has open (docs/04 "What a chat has open"): the chat.resource server binding, through which another plugin
// says a chat uses one of its tabs - the browser's page an AI drives - or no longer does. The chat keeps the list
// (chat.json), its topic says when it changes (resources.changed) and the side panel lists them to open.

include "service.ch"

namespace Ai
{
	void ProvideChatResources(IED::PluginContext& context, Chats* chats);
}
