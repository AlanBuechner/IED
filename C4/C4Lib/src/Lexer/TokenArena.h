#pragma once
#include "Token.h"
#include <cstddef>
#include <cstring>

namespace Scripting
{

	/// Contiguous token arena that allocates tokens via pointer bumping.
	/// Tokens are stored in a single contiguous buffer so there is no heap
	/// fragmentation from individual allocations.  Supports discarding all
	/// tokens allocated beyond a given byte offset inside the arena buffer,
	/// which is useful for undo / speculative-parse re-tokenisation.
	class TokenArena
	{
	public:
		/// @param capacity Maximum number of tokens before reallocation.
		explicit TokenArena(uint32 capacity = 4096);

		~TokenArena();

		// Non-copyable, movable.
		TokenArena(const TokenArena&) = delete;
		TokenArena& operator=(const TokenArena&) = delete;
		TokenArena(TokenArena&& other) noexcept;
		TokenArena& operator=(TokenArena&& other) noexcept;

		/// Allocate one token slot and return a pointer to the default-constructed
		/// Token.  Returns nullptr if the arena is full (capacity reached).
		Token* Alloc();

		/// Return a reference to the token at the given zero-based index.
		Token& At(uint32 index) const;

		/// Number of tokens currently allocated in the arena.
		uint32 Count() const { return m_Count; }

		/// Maximum number of tokens before the arena is considered full.
		uint32 Capacity() const { return m_Capacity; }

		/// Whether the arena has no allocated tokens.
		bool Empty() const { return m_Count == 0; }

		/// Discard all tokens whose arena slot starts at or after *byteOffset*
		/// bytes from the beginning of the buffer.  Tokens before the offset
		/// are preserved in-place.
		void DiscardFromByteOffset(size_t byteOffset);

		/// Discard all tokens at or beyond the given zero-based index.
		/// Tokens before the index are preserved in-place.
		void DiscardFromIndex(uint32 index);

		/// Discard all tokens strictly before the given zero-based index,
		/// shifting remaining tokens down so that token at *index* becomes index 0.
		/// Used by ApplyUndo to discard committed/consumed tokens.
		void CompactFromIndex(uint32 index);

		/// Reset the arena: discard all tokens without freeing the buffer.
		void Clear();

		/// Free all resources including the backing buffer.
		void Release();

	private:
		Token* m_Buffer = nullptr; ///< Contiguous token storage.
		Token* m_WritePtr = nullptr;///< Pointer-bump cursor (next free slot).
		uint32  m_Count = 0;        ///< Number of allocated tokens.
		uint32  m_Capacity = 0;     ///< Maximum token slots in the buffer.

		void Grow(uint32 newCapacity);
	};

} // namespace Scripting
