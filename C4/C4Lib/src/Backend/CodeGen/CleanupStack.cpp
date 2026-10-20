#include "Backend/CodeGen/CleanupStack.h"

namespace Scripting
{
	CleanupEntry CleanupStack::Pop()
	{
		CleanupEntry top = m_Entries.back();
		m_Entries.pop_back();
		return top;
	}

	void CleanupStack::PopTo(size_t depth)
	{
		if (depth < m_Entries.size()) m_Entries.resize(depth);
	}

	std::vector<CleanupEntry> CleanupStack::DetachFrom(size_t depth)
	{
		std::vector<CleanupEntry> tail(m_Entries.begin() + depth, m_Entries.end());
		m_Entries.resize(depth);
		return tail;
	}

	void CleanupStack::Restore(std::vector<CleanupEntry>&& entries)
	{
		for (CleanupEntry& e : entries) m_Entries.push_back(e);
	}

	llvm::CatchPadInst* CleanupStack::InnermostHandler(size_t floor) const
	{
		for (size_t i = m_Entries.size(); i-- > floor; )
			if (m_Entries[i].kind == CleanupEntry::Kind::Handler) return m_Entries[i].handlerPad;
		return nullptr;
	}
} // namespace Scripting
