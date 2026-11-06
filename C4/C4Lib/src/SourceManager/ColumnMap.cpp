#include "ColumnMap.h"

namespace Scripting
{
	void ColumnMap::Add(uint32 line, std::vector<ExpandedSpan> spans)
	{
		if (!spans.empty()) m_Lines[line] = std::move(spans);
	}

	uint32 ColumnMap::Written(uint32 line, uint32 column) const
	{
		const auto found = m_Lines.find(line);
		if (found == m_Lines.end() || column == 0) return column;
		const uint32 at = column - 1;
		uint32 written = at;
		for (const ExpandedSpan& span : found->second)
		{
			if (span.expandedStart > at) break;
			written = at < span.expandedEnd ? span.writtenStart : span.writtenEnd + (at - span.expandedEnd);
		}
		return written + 1;
	}

	uint32 ColumnMap::Expanded(uint32 line, uint32 column) const
	{
		const auto found = m_Lines.find(line);
		if (found == m_Lines.end() || column == 0) return column;
		const uint32 at = column - 1;
		uint32 expanded = at;
		for (const ExpandedSpan& span : found->second)
		{
			if (span.writtenStart > at) break;
			expanded = at < span.writtenEnd ? span.expandedStart : span.expandedEnd + (at - span.writtenEnd);
		}
		return expanded + 1;
	}
}
