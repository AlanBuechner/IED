#include "Features/CompletionCandidates.h"
#include "Features/Completion.h"
#include <cctype>

namespace C4LSP::Features
{
	bool CompletionCandidates::StartsWith(std::string_view name, std::string_view prefix)
	{
		if (prefix.size() > name.size()) return false;
		for (size_t i = 0; i < prefix.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
		return true;
	}

	bool CompletionCandidates::Wants(std::string_view name) const
	{
		return !name.empty() && StartsWith(name, m_Prefix) && !m_Offered.contains(name);
	}

	void CompletionCandidates::Keep(std::string name, int kind, std::string detail)
	{
		m_Offered.insert(name);
		m_Candidates.Push(Candidate{ std::move(name), kind, std::move(detail) });
	}

	Core::Vector<CompletionItem> CompletionCandidates::Items() const
	{
		Core::Vector<CompletionItem> items;
		for (const Candidate& candidate : m_Candidates)
			items.Push(CompletionItem{ candidate.name, candidate.kind, candidate.detail });
		return items;
	}
} // namespace C4LSP::Features
