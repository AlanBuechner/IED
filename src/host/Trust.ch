// TrustStore (docs/11 "Trust layers"): the folders the owner trusts, kept in <app data>/trust.json - never in a
// repository, which cannot trust itself. A workspace opens untrusted unless its root is in the store; nothing a
// project defines runs until it is trusted.

include "bedrock.ch"

namespace Ied
{
	class TrustStore
	{
		[operator("create")] public void Init() {}

		// The store in `file`; a missing file trusts nothing, an unreadable one too (and says so in `error`).
		public void Load(Core::StringView file, Core::String& error);
		public bool IsTrusted(const FS::Path& root) const;
		// Records the owner's choice and writes the file; false when it could not be written. Only the master writes it
		// (docs/01 "Processes").
		public bool Set(const FS::Path& root, bool trusted);
		// The choice kept without writing the file: a workspace server's, the master writing it.
		public void Remember(const FS::Path& root, bool trusted);
		// The file read again (another process wrote it).
		public void Reload(Core::String& error);

		private Core::String m_File;
		private Core::HashMap<Core::String, bool> m_Roots;   // TrustKey -> trusted
	}

	// A root as the store keys it - and as the server tells one folder from another (recent projects, workspace ids,
	// local and server state): '/' separators, no trailing '/', ASCII case folded where names ignore case (Windows, not
	// Linux: fs/PathCase.ch).
	Core::String TrustKey(Core::StringView root);
	// TrustKey with case folded on every system, as it was before 2026-11-21: what an older Linux build keyed by, so
	// what it kept is found again (trust.json version 1, a folder's local state, a workspace server's state).
	Core::String FoldedTrustKey(Core::StringView root);
}
