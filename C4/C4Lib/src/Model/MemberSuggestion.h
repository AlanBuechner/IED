#pragma once
#include "Core/Core.h"
#include <vector>

namespace Scripting
{
	/// The member a use of `name` most likely meant (BUG-361), of `members`: the C4 form of a C++ standard library
	/// name (push_back is Push, size is Count, c_str is CStr, ...), else one spelled the same but for case and
	/// underscores, else one a typo away. Empty when none is close.
	Core::String SuggestedMember(const Core::String& name, const std::vector<Core::String>& members);
} // namespace Scripting
