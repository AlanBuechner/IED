// The chat's context meter (docs/04 "Context management"): how full the model's context is after the last step - the
// tokens its server counted for the request plus the reply it gave - against the window of the model that counted
// them, and what fills it: estimates of the system message, the tool definitions, the messages and the tool results,
// scaled so they add up to the count; for Claude, the CLI's own rows (get_context_usage) once a turn ends, the growth
// of later steps added to its Messages row. It is the context.changed data, kept in chat.json, and redrawn for another
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

	// {window, used, model, compressAt?, compacts?, source?, parts: [{name, tokens}]}: IED compresses a local model's
	// context at `compressAt` percent; Claude compacts its own (`compacts`); `source` "claude" when the parts are the
	// Claude CLI's own rows.
	Core::String ContextJson(int64 window, int64 used, Core::StringView model, bool claude, const Core::Array<ContextPart>& parts, Core::StringView source = Core::StringView(""));
	// The Claude CLI's get_context_usage answer read: its total, its window (maxTokens, else rawMaxTokens) and the rows
	// that fill the window - kind "used" (a CLI before kinds: any row but "Free space" and "Autocompact buffer"). False
	// when it counted nothing.
	bool ReadClaudeContext(const Serialization::DomNode& usage, int64& used, int64& window, Core::Array<ContextPart>& parts);
	// The parts of meter `context` when they are the CLI's rows, with `used` less their sum added to its Messages row
	// (what the steps since added); empty when they are not.
	Core::Array<ContextPart> GrownClaudeParts(Core::StringView context, int64 used);
	// `parts` scaled to add up to `used`, the last taking what rounding leaves; as they are when either is 0.
	Core::Array<ContextPart> ScaledParts(const Core::Array<ContextPart>& parts, int64 used);
	// The meter `context` again for another model: its window and model replaced, its count and parts kept.
	Core::String ContextForModel(Core::StringView context, int64 window, Core::StringView model, bool claude);
	// The model meter `context` counted for; "" for none.
	Core::String ContextModel(Core::StringView context);
}
