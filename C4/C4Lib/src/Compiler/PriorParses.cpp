#include "Compiler/PriorParses.h"

namespace Scripting
{
	const PriorParse* PriorParses::Find(uint32 sourceId, const Source& source) const
	{
		const auto found = m_Parses.find(sourceId);
		if (found == m_Parses.end() || !found->second.source || !found->second.source->SameTextAs(source)) return nullptr;
		return &found->second;
	}

	void PriorParses::Keep(uint32 sourceId, PriorParse parse)
	{
		m_Parses[sourceId] = std::move(parse);
	}
} // namespace Scripting
