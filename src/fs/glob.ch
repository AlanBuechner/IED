// Globs (docs/03 solution projects): '*' is any run of characters within one path segment, '?' one character, and a
// "**" segment any number of whole segments; '/' (or '\') separates segments. Matching ignores ASCII case, as Windows
// paths do.
//     Ied::GlobMatch(Core::StringView("build/**/*.ied.proj"), Core::StringView("build/ied/Game.ied.proj"))   // true

include "bedrock.ch"

namespace Ied
{
	bool GlobMatch(Core::StringView pattern, Core::StringView path);
	// Whether `pattern` has a wildcard at all.
	bool IsGlob(Core::StringView pattern);
	// The files under `root` whose root-relative paths ('/' separators) match `pattern`, in name order; a repository's
	// own folder (.git) is not searched.
	Core::Array<Core::String> ExpandGlob(const FS::Path& root, Core::StringView pattern);
}
