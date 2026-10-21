// Merging one tier's value over another's (docs/07 "The effective value"): replace, objects merged member by member
// (deeper tiers winning, nested objects merged too), or lists appended.

include "catalog.ch"

namespace Ied
{
	// `over` applied to `base` (both JSON text) by `kind`; a merge or an append of values that are not both objects
	// or both lists is a replace.
	Core::String MergeJson(Core::StringView base, Core::StringView over, MergeKind kind);
}
