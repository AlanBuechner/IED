// Language server profiles (docs/24 "Profiles and selection"): what a plugin's `lspProfiles` contribution says, and
// which profile serves a document - the last loaded one whose selector matches it (its language id, then its
// extension, then a glob); the caller checks `when`.

include "positions.ch"

namespace Ied
{
	class LspProfile
	{
		public Core::String id;
		public Core::String label;
		public Core::String pluginId;
		public FS::Path pluginDir;
		public Core::String command;                    // with ${...} variables (ExpandProfileVariables)
		public Core::Array<Core::String> args;
		public Core::Array<Core::String> environment;   // "NAME=value"
		public Core::String cwd;
		public Core::Array<Core::String> languages;
		public Core::Array<Core::String> extensions;
		public Core::Array<Core::String> globs;
		public Core::String initializationOptions;      // JSON text, "{}" without any
		public Core::String initializationOptionsFile;
		public bool staged;
		public bool perProject;                         // scope "project": one server per project of a solution
		public bool negotiate;                          // offer utf-16 and utf-8 and take what the server picks
		public PositionEncoding encoding;               // when not negotiated
		public bool integerIds;
		public Core::Array<Core::String> refetch;       // refetchOnDiagnostics
		public bool filterCodeActions;
		public Core::String when;
	}

	// The variables a profile's command, arguments and folders may use.
	// A setting's effective value for the workspace as JSON text; empty when it has none.
	alias ConfigLookup = func<Core::String(Core::StringView key)>;

	struct ProfileVariables
	{
		public Core::String pluginDir;
		public Core::String workspaceDir;
		public Core::String solutionDir;   // the workspace folder when it is no solution
		public Core::String projectName;   // empty for a workspace-wide server
		public Core::String projectDir;
		public ConfigLookup config;        // ${config:<setting>}; without one they expand to nothing
	}

	// The profiles of one plugin's `contributes`, in order.
	// One lspProfiles entry.
	LspProfile* ReadLspProfile(const Serialization::DomNode& entry, Core::StringView pluginId, const FS::Path& pluginDir);
	void ReadLspProfiles(const Serialization::DomNode& contributes, Core::StringView pluginId, const FS::Path& pluginDir, Core::Array<LspProfile*>& profiles);
	// Whether `profile`'s selector names the document.
	bool ProfileSelects(const LspProfile* profile, Core::StringView languageId, Core::StringView path);
	// The last of `profiles` whose selector names the document, or null.
	LspProfile* ProfileServing(const Core::Array<LspProfile*>& profiles, Core::StringView languageId, Core::StringView path);
	// `text` with ${pluginDir}, ${workspaceDir}, ${solutionDir}, ${projectName} and ${projectDir} expanded; any other
	// ${...} is left as it is.
	Core::String ExpandProfileVariables(Core::StringView text, const ProfileVariables& variables);
	// One of a profile's `args`, expanded: one argument, or one per element when it is exactly one ${config:...} of a
	// list setting (docs/24).
	void ExpandProfileArgument(Core::StringView text, const ProfileVariables& variables, Core::Array<Core::String>& out);
}
