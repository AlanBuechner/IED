#include "Server/ProgramPlan.h"

namespace C4LSP::Server
{
	namespace
	{
		// Whether a candidate holding `count` of what is left, analyzed or not, with `size` projects, beats the best so far.
		struct Candidate
		{
			int32 index = -1;
			size_t count = 0;
			bool analyzed = false;
			size_t size = 0;

			bool Beats(const Candidate& best) const
			{
				if (count != best.count) return count > best.count;
				if (analyzed != best.analyzed) return analyzed;
				return size < best.size;
			}
		};

		size_t CountLeft(const ProgramFacts& program, const std::vector<int32>& assigned)
		{
			size_t count = 0;
			for (size_t d = 0; d < assigned.size(); ++d)
				if (assigned[d] < 0 && d < program.owns.size() && program.owns[d]) ++count;
			return count;
		}

		size_t CountNeeded(const ReachFacts& program, const std::set<std::string>& needed)
		{
			size_t count = 0;
			for (const std::string& project : program.reaching)
				if (needed.count(project)) ++count;
			return count;
		}
	}

	std::vector<int32> AssignDocuments(const std::vector<ProgramFacts>& programs, size_t documents)
	{
		std::vector<int32> assigned(documents, -1);
		for (;;)
		{
			Candidate best;
			for (size_t p = 0; p < programs.size(); ++p)
			{
				const Candidate candidate{ static_cast<int32>(p), CountLeft(programs[p], assigned), programs[p].analyzed, programs[p].projects };
				if (candidate.count > 0 && (best.index < 0 || candidate.Beats(best))) best = candidate;
			}
			if (best.index < 0) return assigned;
			const std::vector<bool>& owns = programs[best.index].owns;
			for (size_t d = 0; d < documents && d < owns.size(); ++d)
				if (assigned[d] < 0 && owns[d]) assigned[d] = best.index;
		}
	}

	std::vector<size_t> ProgramsToConsult(const std::vector<ReachFacts>& programs, int32 asked)
	{
		std::set<std::string> needed;
		for (size_t p = 0; p < programs.size(); ++p)
			if (static_cast<int32>(p) != asked) needed.insert(programs[p].reaching.begin(), programs[p].reaching.end());
		if (asked >= 0 && static_cast<size_t>(asked) < programs.size())
			for (const std::string& held : programs[asked].projects) needed.erase(held);
		std::vector<size_t> chosen;
		while (!needed.empty())
		{
			Candidate best;
			for (size_t p = 0; p < programs.size(); ++p)
			{
				if (static_cast<int32>(p) == asked) continue;
				const Candidate candidate{ static_cast<int32>(p), CountNeeded(programs[p], needed), programs[p].analyzed, programs[p].projects.size() };
				if (candidate.count > 0 && (best.index < 0 || candidate.Beats(best))) best = candidate;
			}
			if (best.index < 0) break;
			chosen.push_back(static_cast<size_t>(best.index));
			for (const std::string& project : programs[best.index].reaching) needed.erase(project);
		}
		return chosen;
	}
} // namespace C4LSP::Server
