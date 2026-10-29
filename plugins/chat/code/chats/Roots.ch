// Other workspaces a chat reaches by an alias (docs/28 "Where it lives"): a tool's path, root or cwd "<alias>:/<path>"
// names <path> in the workspace a plugin's ai.roots binding gives for the alias - the developer plugin's "ied", IED's
// own source - opened when first used. A root may ask that every write and command in it be confirmed.
// And the plugins' folders (docs/13 "Plugin folders"): "plugins:/<plugin id>/<path>" names <path> in the folder of a
// plugin loaded for the chat's workspace - its skills, its reference docs, its manifest. They are read-only: the read
// tools that work on a folder (ToolSpec::folderRoots) read it from the disk, and a write or a command there goes to
// the plugin's folder in a root that holds it (developer mode's ied:/, the workspace's own .ied/plugins/) or is
// refused, as is a write into a loaded plugin's folder by its workspace path.

include "api/Plugin.ch"

namespace Ai
{
	const char* c_PluginsAlias = "plugins";

	// The alias a tool call's arguments name ("ied" for {"path": "ied:/src/a.cf"}), "" for none: a lowercase word of at
	// least two letters before ":/" in its path, root, cwd or program ("C:/" is a drive, not an alias).
	Core::String RootAlias(const Serialization::DomNode& args);
	// The arguments (JSON) with "<alias>:/" taken off their path, root, cwd and program.
	Core::String WithoutAlias(const Serialization::DomNode& args, Core::StringView rootAlias);
	// A permission subject "kind(argument)" naming the root: "fs.write(ied:/src/a.cf)".
	Core::String RootSubject(Core::StringView subject, Core::StringView rootAlias);
	// The same with the argument after `prefix`: "fs.read(plugins:/c4/docs/a.md)" for "plugins:/c4/".
	Core::String PrefixedSubject(Core::StringView subject, Core::StringView prefix);

	class RootTarget
	{
		public IED::Workspace m_Ws;
		public bool m_Confirm;      // every write and command there asks
		public Core::String m_RootAlias;   // the root the permission subject names; "" for the chat's own workspace
		public Core::String m_Args;     // the arguments the tool gets (JSON), the alias taken off
		public Core::String m_Shown;    // what the tool shows its paths under, so they can be passed back: "ied:/"
		public Core::String m_Note;     // what the result says first: where a plugins:/ write went
		public Core::String m_Answer;   // the answer itself when there is nothing to run (plugins:/ listed)
	}

	// The workspace `alias` names for a chat of workspace `wsId`, opened by the ai.roots provider when it is not open;
	// false with `problem`. Without the server lock.
	bool OpenRoot(Core::StringView wsId, Core::StringView rootAlias, RootTarget& target, Core::String& problem);
	// Where a call naming `rootAlias` runs, for a chat of `ws`: an ai.roots root, or plugins:/ - read in the plugin's
	// folder, or written (`writes`: the tool writes or runs a command) through the root that holds it. False with
	// `problem`. Without the server lock.
	bool ResolveRoot(const IED::Workspace& ws, const Serialization::DomNode& args, Core::StringView rootAlias, bool writes, RootTarget& target, Core::String& problem);
	// Whether a write a tool of the chat's own workspace makes at its `path` may go on: not when it lands in the folder
	// of a plugin loaded there that no root holds (`problem` says so). Without the server lock.
	bool MayWriteThere(const IED::Workspace& ws, const Serialization::DomNode& args, Core::String& problem);
	// Whether a tool's workspace is a plugin's folder (plugins:/), which is no workspace of the server's.
	bool IsFolderRoot(const IED::Workspace& ws);
	// The first part of a path ("c4" of "c4/docs/a.md") and the rest.
	void SplitFirst(Core::StringView path, Core::String& first, Core::String& rest);
	// `path` joined under `folder` with "/" ("" stays out).
	Core::String JoinPath(Core::StringView folder, Core::StringView path);
	// Which of the plugins' `ids` plugins:/<name>/ names: the one that is `name`, else the only one ending in ".<name>"
	// ("gpu" for ied.gpu, as its skill is named); -1 for none or several.
	int64 FindPluginId(const Core::Array<Core::String>& ids, Core::StringView name);
}
