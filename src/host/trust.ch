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
		// Records the owner's choice and writes the file; false when it could not be written.
		public bool Set(const FS::Path& root, bool trusted);

		private Core::String file;
		private Core::HashMap<Core::String, bool> roots;   // folded root -> trusted
	}

	// A root as the store keys it: '/' separators, ASCII case folded, no trailing '/'.
	Core::String TrustKey(Core::StringView root);
}
