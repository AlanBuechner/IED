#include "TokenArena.h"

namespace Scripting
{
	Token* TokenArena::Alloc()
	{
		if (m_Count % kChunkTokens == 0) m_Chunks.push_back(std::make_unique<Token[]>(kChunkTokens));
		Token* token = &m_Chunks.back()[m_Count % kChunkTokens];
		++m_Count;
		return token;
	}
} // namespace Scripting
