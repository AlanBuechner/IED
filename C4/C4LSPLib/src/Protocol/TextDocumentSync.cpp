#include "Protocol/TextDocumentSync.h"
#include <algorithm>

namespace C4LSP::Protocol
{
	void TextDocumentSync::ApplyChanges(const std::vector<TextContentChangeEvent>& changes)
	{
		for (const auto& change : changes)
			ApplySingleChange(m_Text, change);
	}

	void TextDocumentSync::ApplySingleChange(std::string& buffer, const TextContentChangeEvent& change)
	{
		size_t startOffset = PositionToOffset(buffer, change.range.start);
		size_t endOffset = PositionToOffset(buffer, change.range.end);
		if (endOffset < startOffset)
			endOffset = startOffset;
		buffer.replace(startOffset, endOffset - startOffset, change.text);
	}

	size_t TextDocumentSync::PositionToOffset(const std::string& buffer, const Position& pos)
	{
		size_t offset = 0;
		uint32 currentLine = 0;
		while (currentLine < pos.line && offset < buffer.size())
		{
			size_t newline = buffer.find('\n', offset);
			if (newline == std::string::npos)
				break;
			offset = newline + 1;
			++currentLine;
		}

		size_t lineEnd = buffer.find('\n', offset);
		if (lineEnd == std::string::npos)
			lineEnd = buffer.size();
		offset += std::min<size_t>(pos.character, lineEnd - offset);
		return offset;
	}
} // namespace C4LSP::Protocol
