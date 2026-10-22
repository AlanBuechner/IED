#pragma once
#include "Core/Core.h" // global template<class T> class Ref (used by ObjectDefinitionNode member lists)
#include "Protocol/LspTypes.h"
#include <string>
#include <unordered_set>

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// One entry in a `workspace/symbol` result. LSP's WorkspaceSymbol is flat (no children):
	/// the enclosing scope, when any, is carried in `containerName`. `kind` uses the numeric
	/// LSP SymbolKind values and `location` points at the declaration name so a client can jump.
	struct WorkspaceSymbol
	{
		std::string name;
		int kind = 12;             // LSP SymbolKind (Function)
		Protocol::Location location;
		std::string containerName; // enclosing namespaces and types, "::"-joined (`Ai::ToolRegistry`); empty at file scope
	};

	class WorkspaceSymbols
	{
	public:
		/// Collects top-level and nested declarations from every compiled source whose name
		/// matches `query` (case-insensitive substring). An empty query returns everything.
		/// Results are capped so a large project never floods the client's symbol picker.
		static Core::Vector<WorkspaceSymbol> ForQuery(Server::CompilationSession& session, const std::string& query);

	private:
		struct Walk
		{
			Server::CompilationSession& session;
			const std::string& query;
			std::unordered_set<std::string> namespaces; // every namespace path of the program
			Core::Vector<WorkspaceSymbol> out;
		};

		// Processes one declaration node: emits it when its name matches and recurses into any
		// nested scopes (namespace/object bodies) so a query finds members of non-matching parents.
		static void AddDeclaration(Walk& walk, const Scripting::AstNode& node, uint32 sourceId, const std::string& container, bool member);
		static void AddEach(Walk& walk, const Core::Vector<Ref<Scripting::AstNode>>& nodes, uint32 sourceId, const std::string& container, bool member);
		static void Push(Walk& walk, const Scripting::AstNode& node, const std::string& container, bool member);
	};
} // namespace C4LSP::Features
