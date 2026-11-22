#include "Compiler/PriorParses.h"

namespace C4Compiler
{
	namespace
	{
		bool SameLayout(const Ref<MacroLayout>& a, const Ref<MacroLayout>& b)
		{
			if (!a || !b) return !a && !b;
			return *a == *b;
		}

		bool SameBindings(const std::vector<std::string>& paths, const SourceInfo& info, const SourceManager& sources)
		{
			if (paths.size() != info.bindings.size()) return false;
			for (size_t i = 0; i < paths.size(); ++i)
				if (sources.GetInfo(info.bindings[i]).absPath != paths[i]) return false;
			return true;
		}
	}

	const PriorParse* PriorParses::Find(uint32 sourceId, const SourceManager& sources) const
	{
		const auto found = m_Parses.find(sourceId);
		if (found == m_Parses.end() || !found->second.source || !sources.HasInfo(sourceId)) return nullptr;
		const SourceInfo& info = sources.GetInfo(sourceId);
		const PriorParse& parse = found->second;
		if (!info.source || !parse.source->SameTextAs(*info.source) || !SameLayout(parse.macros, info.macros) ||
			!SameBindings(parse.bindings, info, sources))
			return nullptr;
		return &parse;
	}

	void PriorParses::Keep(uint32 sourceId, PriorParse parse)
	{
		m_Parses[sourceId] = std::move(parse);
	}
} // namespace C4Compiler
