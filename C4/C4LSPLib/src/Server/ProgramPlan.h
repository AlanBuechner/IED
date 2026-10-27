#pragma once
#include "Core/Core.h"
#include <set>
#include <string>
#include <vector>

namespace C4LSP::Server
{
	// What the planner knows of one program when it places the open documents.
	struct ProgramFacts
	{
		std::vector<bool> owns;   // per document: whether one of its projects' sources holds it
		bool analyzed = false;    // it has an analysis already
		size_t projects = 0;      // how many projects it describes
	};

	// Which program analyzes each of `documents` documents: an index into `programs`, or -1 when none holds it. As few
	// programs as hold them all, chosen one at a time - the one holding the most documents left, an analyzed one before
	// one that is not, then the one with fewer projects, then the first.
	std::vector<int32> AssignDocuments(const std::vector<ProgramFacts>& programs, size_t documents);

	// What a query across programs knows of one program.
	struct ReachFacts
	{
		std::set<std::string> reaching;   // the projects the declaration asked about reaches in it
		std::set<std::string> projects;   // all its projects
		bool analyzed = false;
	};

	// The programs besides `asked` (-1: none) a query about a declaration asks too: as few as hold every project it
	// reaches that `asked` does not hold, chosen as AssignDocuments chooses.
	std::vector<size_t> ProgramsToConsult(const std::vector<ReachFacts>& programs, int32 asked);
} // namespace C4LSP::Server
