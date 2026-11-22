#pragma once
#include <string>

namespace C4Compiler
{
	/// The edits from `a` to `b` - a character added, dropped or changed, or two neighbors swapped - case-sensitive: `Test` is
	/// one edit from `test`, and so is `Rnage` from `Range`.
	size_t EditDistance(const std::string& a, const std::string& b);

	/// Whether `written` is probably a typo of `name` (D-42): not it, and one edit from a name shorter than 6 characters or two
	/// from a longer one.
	bool IsNearMiss(const std::string& written, const std::string& name);
} // namespace C4Compiler
