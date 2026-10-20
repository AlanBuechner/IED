#include "Source.h"
#include <fstream>
#include <algorithm>

namespace Scripting
{
	// ---------------------------------------------------------------------------
	// Source - synchronous source text with direct pointer access and line table
	//
	// Reads the entire source into memory at construction time. Builds a precomputed
	// line table for O(1) line-to-byte-offset lookups. No threads, mutexes, or
	// ring buffer overhead.
	// ---------------------------------------------------------------------------

	// Strip a leading UTF-8 byte-order mark (EF BB BF) if present. Editors such as Visual
	// Studio and Notepad save files with a BOM by default; left in place it glues to the
	// first token (e.g. "int" -> "Unknown type"). Only a BOM at byte 0 is removed - a
	// U+FEFF later in the text is legitimate content and is preserved.
	static void StripUtf8Bom(std::string& text)
	{
		if (text.size() >= 3 &&
			static_cast<unsigned char>(text[0]) == 0xEF &&
			static_cast<unsigned char>(text[1]) == 0xBB &&
			static_cast<unsigned char>(text[2]) == 0xBF) {
			text.erase(0, 3);
		}
	}

	Source::Source(const fs::path& path) : m_ReadPosition(0)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			m_Size = 0;
			return;
		}

		size_t fileSize = static_cast<size_t>(file.tellg());
		file.seekg(0, std::ios::beg);

		m_Data.resize(fileSize);
		if (fileSize > 0)
		{
			file.read(m_Data.data(), static_cast<std::streamsize>(fileSize));
		}
		StripUtf8Bom(m_Data);
		m_Size = m_Data.size();
		BuildLineTable();
	}

	Source::Source(const std::string& content) : m_Data(content), m_ReadPosition(0)
	{
		StripUtf8Bom(m_Data);
		m_Size = m_Data.size();
		BuildLineTable();
	}

	Ref<Source> Source::FromMemory(const std::string& content)
	{
		return Ref<Source>(new Source(content));
	}

	void Source::BuildLineTable()
	{
		m_LineTable.Clear();
		if (m_Size == 0) return;

		// First line always starts at byte 0.
		m_LineTable.Push(0);

		for (size_t i = 0; i < m_Size; ++i)
		{
			if (m_Data[i] == '\n')
			{
				// Next line starts at the byte after this newline.
				m_LineTable.Push(i + 1);
			}
		}
	}

	// ---- Public API ----------------------------------------------------------

	int Source::ReadChar()
	{
		if (m_ReadPosition >= m_Size)
			return -1; // EOF

		unsigned char c = static_cast<unsigned char>(m_Data[m_ReadPosition]);
		++m_ReadPosition;
		return static_cast<int>(c);
	}

	void Source::Seek(size_t offset)
	{
		if (offset > m_Size)
			offset = m_Size;
		m_ReadPosition = offset;
	}

	void Source::PushBack()
	{
		if (m_ReadPosition > 0)
			--m_ReadPosition;
	}

	int Source::ReadCharAt(size_t offset) const
	{
		if (offset >= m_Size)
			return -1;
		return static_cast<int>(static_cast<unsigned char>(m_Data[offset]));
	}

	Core::String Source::GetLineText(uint32 line) const
	{
		if (line == 0) return Core::String();

		size_t idx = static_cast<size_t>(line - 1);
		if (idx >= m_LineTable.Count())
			return Core::String();

		size_t lineStart = m_LineTable[idx];
		bool hasTrailingNewline = idx + 1 < m_LineTable.Count();
		size_t lineEnd = hasTrailingNewline ? m_LineTable[idx + 1] : m_Size;

		// A newline-terminated line excludes the newline; a final line without one keeps all its chars.
		if (hasTrailingNewline)
			--lineEnd;

		size_t length = lineEnd - lineStart;
		return Core::String(m_Data.data() + lineStart, length);
	}

	size_t Source::LineToByteOffset(uint32 line) const
	{
		if (line == 0) return 0;

		size_t idx = static_cast<size_t>(line - 1);
		if (idx >= m_LineTable.Count())
			return m_Size;

		return m_LineTable[idx];
	}

	void Source::ByteOffsetToLineColumn(size_t byteOffset, uint32& line, uint32& column) const
	{
		line = 1;
		column = 1;

		if (m_LineTable.Empty())
			return; // empty source - line 1, column 1

		// Binary search: find the last entry in m_LineTable that is <= byteOffset.
		// That entry's index + 1 is the 1-based line number.
		size_t lo = 0;
		size_t hi = m_LineTable.Count() - 1;
		while (lo < hi)
		{
			size_t mid = lo + (hi - lo + 1) / 2; // upper midpoint to avoid infinite loop
			if (m_LineTable[mid] <= byteOffset)
				lo = mid;
			else
				hi = mid - 1;
		}

		// lo is now the index of the line containing byteOffset.
		line = static_cast<uint32>(lo + 1); // convert to 1-based
		column = static_cast<uint32>(byteOffset - m_LineTable[lo] + 1); // 1-based column
	}
}
