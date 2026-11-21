#pragma once
#include "Core/Core.h"
#include <string>
#include <cstddef>

namespace Scripting
{
	/// One file's text, as written, with a precomputed line table. Never changed once made: a compile reads the file through
	/// what its macro pass made of it (MacroLayout, R8), so locations are always the file's own.
	class Source : public Refable
	{
	public:
		explicit Source(const fs::path& path);
		static Ref<Source> FromMemory(const std::string& content);
		~Source() = default;

		Source(const Source&)            = delete;
		Source& operator=(const Source&) = delete;

		/// Direct O(1) character access at arbitrary offset. Returns -1 if offset >= size.
		int ReadCharAt(size_t offset) const;

		/// Total number of bytes in the source text.
		size_t Size() const { return m_Size; }

		/// Pointer to the underlying source text (valid for the lifetime of this object).
		const char* Data() const { return m_Data.c_str(); }
		std::string_view Text() const { return m_Data; }

		/// Byte offset where the given 1-based line starts. Returns size for out-of-range lines.
		size_t LineToByteOffset(uint32 line) const;

		/// Convert a byte offset to 1-based line and column using the precomputed line table.
		void ByteOffsetToLineColumn(size_t byteOffset, uint32& line, uint32& column) const;
		/// The byte offset of 1-based `line` and `column` (an editor's position), clamped to the text.
		size_t LineColumnToByteOffset(uint32 line, uint32 column) const;

		/// Text content of the given 1-based line (excluding the trailing newline).
		Core::String GetLineText(uint32 line) const;

		/// Whether `other` holds the same text.
		bool SameTextAs(const Source& other) const { return m_Data == other.m_Data; }

	private:
		explicit Source(const std::string& content);

		void BuildLineTable();

		std::string m_Data;
		size_t m_Size = 0;
		Core::Vector<size_t> m_LineTable;
	};
}
