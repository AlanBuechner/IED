#include "Server/PlaceInAnalysis.h"
#include <algorithm>

namespace C4LSP::Server
{
	namespace
	{
		// The byte offset of `position` in `text`, as NodeLocator and Completion count it: a column past its line's end runs on.
		size_t OffsetOf(std::string_view text, const Protocol::Position& position)
		{
			size_t offset = 0;
			for (uint32 line = 0; line < position.line; ++line)
			{
				const size_t newline = text.find('\n', offset);
				if (newline == std::string_view::npos) return text.size();
				offset = newline + 1;
			}
			return std::min<size_t>(offset + position.character, text.size());
		}

		Protocol::Position PositionOf(std::string_view text, size_t offset)
		{
			Protocol::Position position;
			size_t lineStart = 0;
			for (size_t newline = text.find('\n'); newline != std::string_view::npos && newline < offset; newline = text.find('\n', lineStart))
			{
				++position.line;
				lineStart = newline + 1;
			}
			position.character = static_cast<uint32>(offset - lineStart);
			return position;
		}

		size_t CommonPrefix(std::string_view a, std::string_view b)
		{
			const size_t most = std::min(a.size(), b.size());
			size_t length = 0;
			while (length < most && a[length] == b[length]) ++length;
			return length;
		}

		// The common end of `a` and `b` that does not reach into their first `prefix` bytes.
		size_t CommonSuffix(std::string_view a, std::string_view b, size_t prefix)
		{
			const size_t most = std::min(a.size(), b.size()) - prefix;
			size_t length = 0;
			while (length < most && a[a.size() - 1 - length] == b[b.size() - 1 - length]) ++length;
			return length;
		}
	}

	PlaceInAnalysis PlaceInAnalyzedText(std::string_view analyzed, std::string_view now, const Protocol::Position& position)
	{
		const size_t offset = OffsetOf(now, position);
		const size_t prefix = CommonPrefix(analyzed, now);
		if (offset <= prefix) return { PositionOf(analyzed, offset), false };
		const size_t suffix = CommonSuffix(analyzed, now, prefix);
		if (offset >= now.size() - suffix) return { PositionOf(analyzed, offset - now.size() + analyzed.size()), false };
		return { PositionOf(analyzed, prefix), true };
	}
} // namespace C4LSP::Server
