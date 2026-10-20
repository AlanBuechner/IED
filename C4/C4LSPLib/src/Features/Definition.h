#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"

namespace Scripting { class AstNode; class ProgramModel; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Go-to-definition and reference resolution shared by definition, references, and highlight.
	class Definition
	{
	public:
		static Core::Vector<Protocol::Location> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

		/// Go-to-definition for an include directive: when the caret sits inside `include "path.ch"`,
		/// resolve that header exactly like the compiler does and point at its first line. Returns
		/// empty otherwise (or when the path cannot be resolved), so callers fall through to normal
		/// symbol resolution. This is what makes Ctrl+left-click / F12 on an include string navigate
		/// to the included file, matching Visual Studio's behavior for #include lines.
		static Core::Vector<Protocol::Location> IncludeTarget(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

		/// Resolve the declaration node for the symbol under `position` (null when not on a symbol).
		/// Shared by go-to-definition, find-references, and document-highlight.
		static const Scripting::AstNode* FindTargetDeclaration(Server::CompilationSession& session, uint32 sourceId, const Server::LocatedNode& located);

		/// Build a location for a declaration node, using the owning unit's URI (editor coords).
		/// Shared by definition, type-definition, implementation, and rename.
		static Protocol::Location LocationForDeclaration(Server::CompilationSession& session, const Scripting::AstNode& declaration);

	private:
		/// Walk the located path (deepest first) for a reference node with a recorded declaration.
		static Core::Vector<Protocol::Location> ResolveReference(Server::CompilationSession& session, uint32 sourceId, const Server::LocatedNode& located);

		/// Project-wide last resort: find declarations named like the identifier under the caret by
		/// scanning every unit's symbol table (for symbols defined in files this one does not include).
		static Core::Vector<Protocol::Location> ProjectWideByName(Server::CompilationSession& session, const Server::LocatedNode& located);
	};
} // namespace C4LSP::Features
