#pragma once
#include "Core/Core.h"
#include "Features/DefinitionWriter.h"
#include "Protocol/LspTypes.h"
#include <optional>
#include <string>
#include <vector>

namespace Scripting { struct FunctionDefinitionNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Where in a file a definition goes, and how that file writes code.
	struct DefinitionSite
	{
		uint32 sourceId = 0;
		std::string uri;
		Protocol::Position at;               // the insertion point, in editor coordinates
		std::string lead;                    // what separates the definitions from the code before them
		std::string trail;                   // and from the code after them
		std::vector<std::string> namespaces; // around the definitions once in (those `wrap` opens too), outermost first
		std::vector<std::string> wrap;       // to open around the definitions: at the end of a file with no block of them
		DefinitionStyle style;
	};

	/// Where the definition of a function declared without a body goes, as Visual Assist's Create Implementation puts it:
	/// in the file the functions declared beside it are defined in, right after the nearest one declared before it, else
	/// right before the nearest one declared after it; with none of them there, at the end of the file's block of its
	/// namespaces, else at the file's end in a block of its own. The file is the one most of them are defined in, else the
	/// source beside its header named like it (`PlayerControler.ch` -> `PlayerControler.cf`).
	class DefinitionSites
	{
	public:
		static std::optional<DefinitionSite> InSourceFile(Server::CompilationSession& session, const Scripting::FunctionDefinitionNode& declaration);
		/// How a body written in place of the declaration's `;` looks: as the bodies beside it are written.
		static DefinitionStyle InPlaceStyle(Server::CompilationSession& session, const Scripting::FunctionDefinitionNode& declaration);
		/// What goes in at the site for these definitions, in order: in the namespaces it opens, apart from the code around
		/// it. `caret` is where the first one's cursor lands in it.
		static std::string Joined(const DefinitionSite& site, const std::vector<WrittenDefinition>& definitions, size_t& caret);
		/// The file a uri names, as a title says it: `PlayerControler.cf`.
		static std::string FileName(const std::string& uri);
	};
} // namespace C4LSP::Features
