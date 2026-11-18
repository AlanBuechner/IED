#pragma once
#include "Features/Completion.h" // CompletionItem
#include <optional>
#include <string>

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Completion of the path an include names (docs/tools/03 "Completion"): the folders and headers of every folder the
	/// compiler's lookup searches from the including file (docs/language/15-projects.md rule 3), nearest first. A folder
	/// goes in with its `/` and asks for completion again, so a path is completed part by part.
	class IncludeCompletion
	{
	public:
		/// The caret inside an include's path: `include "Http/se|"`, or a header the C bindings read,
		/// `extern "c" include "win|"`.
		struct Site
		{
			std::string typed;         // the path from its opening quote to the caret ("Http/se")
			uint32 line = 0;
			uint32 partStart = 0;      // column (UTF-8 bytes) where the part being typed starts: past the last '/'
			uint32 partEnd = 0;        // where it ends: the caret, or the end of a name already written after it
			bool bindings = false;     // a C or C++ header (`extern "c" include`), not a C4 one
			bool slashFollows = false; // the path goes on after the part (`Ht|tp/server.ch`): a folder adds no second '/'
		};

		/// The include path the caret at `position` of `text` is in; nothing when it is in none.
		static std::optional<Site> Find(const std::string& text, const Protocol::Position& position);

		/// What can come next in `site`'s path from the file at `uri`: the folders and headers in the folder its path
		/// names so far, under each folder an include of that file searches, starting with what was typed (any case).
		/// A name two folders have is offered once, as the nearest has it.
		static Core::Vector<CompletionItem> Items(const Server::CompilationSession& session, const std::string& uri, const Site& site);
	};
} // namespace C4LSP::Features
