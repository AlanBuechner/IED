#pragma once
#include "Core/Core.h"
#include <string>
#include <cstddef>

namespace Scripting
{
	/// Synchronous source text holder with direct pointer access and a precomputed line table.
	/// Replaces the async SourceStream ring buffer - no threads, mutexes, or prefetch overhead.
	/// Maintains a read position for streaming APIs (ReadChar / PushBack / Seek) used by the Lexer.
	class Source : public Refable
	{
	public:
		explicit Source(const fs::path& path);
		static Ref<Source> FromMemory(const std::string& content);
		~Source() = default;

		Source(const Source&)            = delete;
		Source& operator=(const Source&) = delete;

		/// Read the character at the current read position and advance. Returns -1 on EOF.
		int ReadChar();

		/// Seek the read position to a byte offset (clamped to source size).
		void Seek(size_t offset);

		/// Push back one character (decrement read position by one, clamped to 0).
		void PushBack();

		/// Direct O(1) character access at arbitrary offset. Returns -1 if offset >= size.
		int ReadCharAt(size_t offset) const;

		/// Current read position (byte offset).
		size_t GetReadPosition() const { return m_ReadPosition; }

		/// Total number of bytes in the source text.
		size_t Size() const { return m_Size; }

		/// Pointer to the underlying source text (valid for the lifetime of this object).
		const char* Data() const { return m_Data.c_str(); }

		/// Byte offset where the given 1-based line starts. Returns size for out-of-range lines.
		size_t LineToByteOffset(uint32 line) const;

		/// Convert a byte offset to 1-based line and column using the precomputed line table.
		void ByteOffsetToLineColumn(size_t byteOffset, uint32& line, uint32& column) const;

		/// Text content of the given 1-based line (excluding the trailing newline).
		Core::String GetLineText(uint32 line) const;

	private:
		explicit Source(const std::string& content);

		void BuildLineTable();

		std::string m_Data;
		size_t m_Size = 0;
		mutable Core::Vector<size_t> m_LineTable;
		size_t m_ReadPosition = 0;
	};
}
