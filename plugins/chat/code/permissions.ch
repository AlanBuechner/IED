// Whether a tool call runs, asks, or is refused (docs/11 "AI permission model"): the ai.permissions rules - deny, then
// confirm - then the chat's permission mode, then the allow rules and what the owner allowed for the chat. A call is
// described by its subject: "fs.read(<path>)", "fs.write(<path>)", "shell(<command>)", "task(<name>)". A rule names a
// kind (with '*', "fs.*") and a pattern: a glob over the path for fs, a wildcard over the text ('*' any run) otherwise.

include "chat.ch"

namespace Ai
{
	enum class ToolClass { Read, Write, Exec, Meta }
	enum class Permit { Allow, Ask, Deny }

	class PermissionRules
	{
		public Core::Array<Core::String> allow;
		public Core::Array<Core::String> confirm;
		public Core::Array<Core::String> deny;
	}

	// The rules of the ai.permissions value (JSON).
	PermissionRules ReadPermissionRules(Core::StringView json);
	bool RuleMatches(Core::StringView rule, Core::StringView subject);
	// What the call `subject` of class `toolClass` may do; `why` gets the reason the approval card shows.
	Permit Decide(PermissionMode mode, ToolClass toolClass, const PermissionRules& rules, const Core::Array<Core::String>& allowedForChat, Core::StringView subject, Core::String& why);
	// The rule "Allow for this chat" adds for `subject`: a write's folder ("fs.write(src/core/**)"), a command's first
	// word ("shell(nuke *)"), anything else itself.
	Core::String ChatAllowRule(Core::StringView subject);
}
