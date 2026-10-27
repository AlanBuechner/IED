// Versions of language tools (docs/26 "Language tools"): comparing dotted versions, what satisfies a pin, and the
// version a workspace pins (go.mod's `go` line, rust-toolchain.toml's channel) as its plugin's `versionFrom` reads it.

include "Spec.ch"

namespace Ied
{
	// <0, 0 or >0: `a` against `b` number by number, from the first digit of each ("1.10" is after "1.9", "1.2" is
	// "1.2.0", "go1.22" is "1.22"); a part ends at anything but a digit, and what follows the numbers is not compared.
	int32 CompareVersions(Core::StringView a, Core::StringView b);
	// Whether `version` starts with a digit: "1.75.0" does, "stable" and "nightly-2024-01-01" do not.
	bool IsNumericVersion(Core::StringView version);
	// Whether a tool of version `found` gives what `wanted` asks for: with `minimum` that version or a newer one,
	// otherwise that version ("1.75" is given by "1.75.0" and "1.75.2", not "1.76"). A wanted version that is no number
	// (a channel: stable, nightly-2024-01-01) is the tool's own business: anything found gives it.
	bool VersionSatisfies(Core::StringView found, Core::StringView wanted, bool minimum);

	// The version a workspace pins for a tool; `version` empty when it pins none.
	struct PinnedVersion
	{
		public Core::String m_Version;
		public bool m_Minimum;
		public Core::String m_File;   // workspace-relative
	}

	// The first of `sources` (in order) whose file in one of `folders` (workspace-relative, "" the root; in order) has a
	// line its regexp matches, group 1 the version.
	PinnedVersion FindPin(const FS::Path& root, const Core::Array<Core::String>& folders, const Core::Array<VersionSource*>& sources);
	// The version an install gives: the pinned one when the install can give it - an exact pin, or a minimum above
	// `fallback` - else `fallback`.
	Core::String VersionToInstall(const PinnedVersion& pin, Core::StringView fallback);
	// The version an install of `spec` gives in a workspace with `pin` (VersionToInstall with the tool's install version),
	// "latest" when neither names one (rustup's component of whatever toolchain is active).
	Core::String ToolInstallVersion(const ToolSpec* spec, const PinnedVersion& pin);
	// Whether `version` may name a folder: letters, digits, '.', '_', '-' and '+', and not only dots.
	bool IsFolderSafeVersion(Core::StringView version);
}
