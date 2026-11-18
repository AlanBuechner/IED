#include "Parser/NearNames.h"
#include <algorithm>
#include <vector>

namespace Scripting
{
	namespace
	{
		constexpr size_t kShortName = 6;      // a name shorter than this is a near miss at one edit
		constexpr size_t kShortEdits = 1;
		constexpr size_t kLongEdits = 2;
	}

	size_t EditDistance(const std::string& a, const std::string& b)
	{
		std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);
		for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;
		for (size_t i = 1; i <= a.size(); ++i)
		{
			current[0] = i;
			for (size_t j = 1; j <= b.size(); ++j)
				current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
			std::swap(previous, current);
		}
		return previous[b.size()];
	}

	bool IsNearMiss(const std::string& written, const std::string& name)
	{
		if (written == name) return false;
		return EditDistance(written, name) <= (name.size() < kShortName ? kShortEdits : kLongEdits);
	}
} // namespace Scripting
