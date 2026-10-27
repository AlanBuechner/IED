// Read-only folders (docs/10 "Read-only folders"): workspace folders and files nobody writes through IED - a person, the
// AI or a plugin - each with the reason and the way out. Any tool names them in a file of its own,
// .ied/local/readonly/<writer>.json: {"paths": [{"path": "<workspace-relative>", "reason": "..."}]}; Nuke names its
// locked packages there and a package clone behind its remote (docs/31). Read when the workspace opens and again when
// one of those files changes.

include "bedrock.ch"

namespace Ied
{
	const char* c_ReadOnlyFolder = ".ied/local/readonly";

	class ReadOnlyRule
	{
		public Core::String m_Path;     // workspace-relative, '/'-separated
		public Core::String m_Reason;
	}

	class ReadOnlyRules
	{
		// The rules the files in `folders` give now; whether they differ from the ones before.
		public bool Load(const Core::Array<FS::Path>& folders);
		// Why `path` may not be written - it is a rule's path or under one - or empty when it may.
		public Core::String ReasonFor(Core::StringView path) const;
		// Why `path` may not be moved or deleted: it is read-only, or something read-only is under it.
		public Core::String ReasonWithin(Core::StringView path) const;
		public Core::String ToJson() const;

		private Core::Array<ReadOnlyRule> m_Rules;
	}

	// Whether a workspace-relative path is one of the files that name read-only folders.
	bool IsReadOnlyFile(Core::StringView path);
}
