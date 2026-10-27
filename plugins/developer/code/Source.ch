// Where developer mode's source is (docs/28): the checkout the server runs from, or the checkout its install was made
// from (install.json, which nuke install-ied writes into the install last), and how far that install is behind it.

include "api/Plugin.ch"

namespace Developer
{
	// What an installed IED was made from: nuke install-ied's install.json.
	class InstallRecord
	{
		public Core::String m_Source;     // the checkout root
		public Core::String m_Commit;     // its HEAD then
		public bool m_Dirty;              // with uncommitted changes
		public int64 m_InstalledMs;
	}

	// Whether `folder` is IED's source checkout: it holds .git, C4/C4Lib/src and src/nuke.toml.
	bool IsCheckout(Core::StringView folder);
	// The record `home` holds; false when it has none (a checkout, or an install made before the record).
	bool ReadInstallRecord(Core::StringView home, InstallRecord& record);
	// The checkout of a server whose home is `home`: `home` itself, or the one its install record names; "" for none.
	Core::String SourceOf(Core::StringView home, const InstallRecord* record);
	// The checkout of this server.
	Core::String SourceFolder();

	// `git rev-parse HEAD` in `checkout`; "" when git cannot say.
	Core::String HeadCommit(Core::StringView checkout);
	// The commits `checkout`'s HEAD has that `commit` lacks (`git rev-list --count <commit>..HEAD`); -1 when unknown.
	int64 CommitsSince(Core::StringView checkout, Core::StringView commit);
}
