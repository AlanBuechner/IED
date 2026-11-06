#include "Protocol/TextDocumentSync.h"
#include <algorithm>

namespace C4LSP::Protocol
{
	void TextDocumentSync::ApplyChanges(const std::vector<TextContentChangeEvent>& changes, PositionEncoding encoding)
	{
		for (const auto& change : changes)
			ApplySingleChange(m_Text, change, encoding);
	}

	void TextDocumentSync::ApplySingleChange(std::string& buffer, const TextContentChangeEvent& change, PositionEncoding encoding)
	{
		if (!change.range)
		{
			buffer = change.text;
			return;
		}
		size_t startOffset = PositionToOffset(buffer, change.range->start, encoding);
		size_t endOffset = PositionToOffset(buffer, change.range->end, encoding);
		if (endOffset < startOffset)
			endOffset = startOffset;
		buffer.replace(startOffset, endOffset - startOffset, change.text);
	}

	size_t TextDocumentSync::PositionToOffset(const std::string& buffer, const Position& pos, PositionEncoding encoding)
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
		const std::string line = buffer.substr(offset, lineEnd - offset);
		const size_t column = encoding == PositionEncoding::Utf16 ? Utf8Length(line, pos.character) : pos.character;
		return offset + std::min(column, line.size());
	}
} // namespace C4LSP::Protocol
