#pragma once
#include "Token.h"
#include <memory>
#include <vector>

namespace Scripting
{
	/// The tokens a lexer has read, in order. They live in chunks that never move, so a token's address (and a
	/// reference the parser holds while it peeks further ahead) stays valid for the arena's life; a chunk's tokens
	/// are made when the lexer first reaches it.
	class TokenArena
	{
	public:
		/// The next token, default-made, at the end.
		Token* Alloc();
		/// Every token, gone: the memory goes back once the parse that read them is over.
		void Clear();

		Token& At(uint32 index) const { return m_Chunks[index / kChunkTokens][index % kChunkTokens]; }
		uint32 Count() const { return m_Count; }
		bool Empty() const { return m_Count == 0; }

	private:
		static constexpr uint32 kChunkTokens = 1024;

		std::vector<std::unique_ptr<Token[]>> m_Chunks;
		uint32 m_Count = 0;
	};
} // namespace Scripting
