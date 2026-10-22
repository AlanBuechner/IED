include "bedrock.ch"

namespace Semver
{
	// A semantic version (semver.org 2.0.0): MAJOR.MINOR.PATCH, then pre-release identifiers after a "-" and build
	// metadata identifiers after a "+".
	struct Version
	{
		public int64 major;
		public int64 minor;
		public int64 patch;
		public Core::Array<Core::String> prerelease;
		public Core::Array<Core::String> build;

		// The version as text: "1.0.0-rc.1+build.5".
		public Core::String ToString() const;
	}

	// Reads "1.2.3", "1.0.0-alpha.1", "1.0.0+build.5" or "1.0.0-rc.1+build.5"; false, leaving `version` as it was,
	// when the text is not a valid version.
	bool ParseVersion(Core::StringView text, Version& version);
	// -1, 0 or 1 as `a` has lower, the same or higher precedence than `b` (semver.org section 11); build metadata
	// does not count.
	int32 Compare(const Version& a, const Version& b);
	// Orders the versions by ascending precedence; versions of the same precedence keep their order.
	void SortVersions(Core::Array<Version>& versions);
}
