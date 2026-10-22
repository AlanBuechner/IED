// Other workspaces a chat reaches by an alias (docs/28 "Where it lives"): a tool's path, root or cwd "<alias>:/<path>"
// names <path> in the workspace a plugin's ai.roots binding gives for the alias - the developer plugin's "ied", IED's
// own source - opened when first used. A root may ask that every write and command in it be confirmed.

include "api/plugin.ch"

namespace Ai
{
	// The alias a tool call's arguments name ("ied" for {"path": "ied:/src/a.cf"}), "" for none: a lowercase word of at
	// least two letters before ":/" in its path, root or cwd ("C:/" is a drive, not an alias).
	Core::String RootAlias(const Serialization::DomNode& args);
	// The arguments (JSON) with "<alias>:/" taken off their path, root and cwd.
	Core::String WithoutAlias(const Serialization::DomNode& args, Core::StringView rootAlias);
	// A permission subject "kind(argument)" naming the root: "fs.write(ied:/src/a.cf)".
	Core::String RootSubject(Core::StringView subject, Core::StringView rootAlias);

	class RootTarget
	{
		public IED::Workspace ws;
		public bool confirm;      // every write and command there asks
	}

	// The workspace `alias` names for a chat of workspace `wsId`, opened by the ai.roots provider when it is not open;
	// false with `problem`. Without the server lock.
	bool OpenRoot(Core::StringView wsId, Core::StringView rootAlias, RootTarget& target, Core::String& problem);
}
