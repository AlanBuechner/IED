#pragma once
#include "Protocol/LspTypes.h"
#include <string_view>

namespace C4LSP::Server
{
	/// Where a place in the editor's text is in the text an analysis compiled, the edits since unknown to it: the same
	/// place before the first change and after the last, and inside the changed text the place it starts at - what was
	/// typed there is not in the analysis, but what encloses it (a function, a type) is.
	struct PlaceInAnalysis
	{
		Protocol::Position position; // line and byte column in the analyzed text
		bool edited = false;         // the place is inside the text changed since
	};

	/// `position` (a line and byte column of `now`, the editor's text) in `analyzed`.
	PlaceInAnalysis PlaceInAnalyzedText(std::string_view analyzed, std::string_view now, const Protocol::Position& position);
} // namespace C4LSP::Server
