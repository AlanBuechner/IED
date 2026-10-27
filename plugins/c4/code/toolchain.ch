// Which C4 toolchain the built-in c4 plugin's tools come from in a workspace (docs/26 "The c4 plugin and its
// toolchains"; a toolchain's own copy, pinned by a Nuke solution, names its toolchain's tools in its manifest): the
// app's c4.toolchain setting, else the toolchain the workspace's nuke.toml pins, else the newest one installed - from
// the toolchain store of the Nuke that IED runs (NUKE_TOOLCHAINS, else <the Nuke checkout>/toolchains), with Nuke's own
// rules for what a pin means (Nuke/nuke/toolchains.py).

include "bedrock.ch"

namespace C4Plugin
{
	// What a choice of toolchain is made from.
	class ToolchainQuery
	{
		public Core::String setting;                  // c4.toolchain: a folder or an installed version ("": unset)
		public Core::String pin;                      // the workspace's [workspace] toolchain ("": none)
		public Core::String root;                     // the workspace's folder
		public Core::String store;                    // <store>/c4, the installed toolchains' folder ("": none found)
		public Core::Array<Core::String> installed;   // the versions installed there
	}

	class ToolchainChoice
	{
		public Core::String folder;                   // "" when no toolchain fits
		public Core::String reason;                   // where it came from, or why there is none
	}

	ToolchainChoice ChooseToolchain(const ToolchainQuery& query);

	// The `toolchain` of a nuke.toml's [workspace] table; "" when it names none.
	Core::String WorkspaceToolchain(Core::StringView manifest);
	// Whether a toolchain name is a folder (Nuke: one with a separator, or starting with '.') rather than a version.
	bool NamesFolder(Core::StringView name);
	// A numbered version, as Nuke installs them: 1.2.3, optionally with -pre or +build.
	bool IsVersion(Core::StringView name);
	// Whether version `a` comes before `b` (by their numbers).
	bool VersionBefore(Core::StringView a, Core::StringView b);
	// The newest numbered version of `names`; "" when there is none.
	Core::String NewestVersion(const Core::Array<Core::String>& names);

	// The Nuke checkout a `nuke.command` setting names when it is a path (<checkout>/bin/nuke.cmd); "" for a bare name.
	Core::String CheckoutOfCommand(Core::StringView command);
	// The files a bare command name may be in on `path` (a PATH value): <folder>/<name>.cmd, .bat, then the name itself.
	Core::Array<Core::String> CommandCandidates(Core::StringView name, Core::StringView path);
	// `entries` (JSON) with every ${c4.toolchain} replaced by `folder`.
	Core::String WithToolchain(Core::StringView entries, Core::StringView folder);

	const char* kToolchainVariable = "${c4.toolchain}";
	const char* kStoreVariable = "NUKE_TOOLCHAINS";
}
