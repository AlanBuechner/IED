// The chat's context meter (docs/04 "Context management"): how full the model's context is after the last step - the
// tokens its server counted for the request plus the reply it gave - against the window of the model that counted
// them, and what fills it: estimates of the system message, the tool definitions, the messages and the tool results,
// scaled so they add up to the count. It is the context.changed data, kept in chat.json, and redrawn for another
// model's window when the chat switches models.

include "bedrock.ch"
include "api/plugin.ch"

namespace Ai
{
	const int64 kContextCompressAtPercent = 85;   // of the window, where IED compresses a local model's context

	class ContextPart
	{
		public Core::String name;
		public int64 tokens;
	}

	// {window, used, model, compressAt?, compacts?, estimated?, parts: [{name, tokens}]}: IED compresses a local model's
	// context at `compressAt` percent; Claude compacts its own (`compacts`); `estimated` when no model counted it.
	Core::String ContextJson(int64 window, int64 used, Core::StringView model, bool claude, const Core::Array<ContextPart>& parts, bool estimated = false);
	// `parts` scaled to add up to `used`, the last taking what rounding leaves; as they are when either is 0.
	Core::Array<ContextPart> ScaledParts(const Core::Array<ContextPart>& parts, int64 used);
	// The meter `context` again for another model: its window and model replaced, its count and parts kept.
	Core::String ContextForModel(Core::StringView context, int64 window, Core::StringView model, bool claude);
	// The model meter `context` counted for; "" for none.
	Core::String ContextModel(Core::StringView context);
}
