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

	// Three rows: a swap of two neighbors reaches back two (BUG-470).
	size_t EditDistance(const std::string& a, const std::string& b)
	{
		std::vector<size_t> beforePrevious(b.size() + 1), previous(b.size() + 1), current(b.size() + 1);
		for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;
		for (size_t i = 1; i <= a.size(); ++i)
		{
			current[0] = i;
			for (size_t j = 1; j <= b.size(); ++j)
			{
				current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
				const bool swapped = i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1];
				if (swapped) current[j] = std::min(current[j], beforePrevious[j - 2] + 1);
			}
			std::swap(beforePrevious, previous);
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
