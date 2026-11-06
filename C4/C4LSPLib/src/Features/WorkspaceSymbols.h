#pragma once
#include "Core/Core.h" // global template<class T> class Ref (used by ObjectDefinitionNode member lists)
#include "Protocol/LspTypes.h"
#include <string>
#include <unordered_set>
#include <vector>

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
		/// Results are capped so a large project never floods the client's symbol picker; a query's
		/// best matches come first and are kept (the name itself, then a prefix; C4 sources before C headers).
		static Core::Vector<WorkspaceSymbol> ForQuery(Server::CompilationSession& session, const std::string& query);

		/// Every declaration and macro of the session's sources, uncapped and unranked: what a program's symbol index keeps
		/// once its analysis is let go.
		static Core::Vector<WorkspaceSymbol> All(Server::CompilationSession& session);
		/// Of `lists` (All's, file by file), the symbols `query` matches, ranked and capped as ForQuery ranks and caps them.
		static Core::Vector<WorkspaceSymbol> Matching(const std::vector<const Core::Vector<WorkspaceSymbol>*>& lists, const std::string& query);

	private:
		struct Walk
		{
			Server::CompilationSession& session;
			const std::string& query;
			std::unordered_set<std::string> namespaces; // every namespace path of the program
			Core::Vector<WorkspaceSymbol> out;
			size_t limit = 0; // where the walk stops collecting: a query collects every match to rank them
		};

		/// The declarations and macros `query` matches, in source order, at most `limit`.
		static Core::Vector<WorkspaceSymbol> Collect(Server::CompilationSession& session, const std::string& query, size_t limit);
		// Processes one declaration node: emits it when its name matches and recurses into any
		// nested scopes (namespace/object bodies) so a query finds members of non-matching parents.
		static void AddDeclaration(Walk& walk, const Scripting::AstNode& node, uint32 sourceId, const std::string& container, bool member);
		static void AddEach(Walk& walk, const Core::Vector<Ref<Scripting::AstNode>>& nodes, uint32 sourceId, const std::string& container, bool member);
		static void Push(Walk& walk, const Scripting::AstNode& node, const std::string& container, bool member);
		/// Every source's `#define`s whose name matches, as Function symbols where they are defined.
		static void AddMacros(Walk& walk);
	};
} // namespace C4LSP::Features
