#pragma once
#include "Core/Core.h" // global template<class T> class Ref (used by ObjectDefinitionNode member lists)
#include "Protocol/LspTypes.h"

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
		std::string containerName; // enclosing type/namespace, " :: "-joined; empty at file scope
	};

	class WorkspaceSymbols
	{
	public:
		/// Collects top-level and nested declarations from every compiled source whose name
		/// matches `query` (case-insensitive substring). An empty query returns everything.
		/// Results are capped so a large project never floods the client's symbol picker.
		static Core::Vector<WorkspaceSymbol> ForQuery(Server::CompilationSession& session, const std::string& query);

	private:
		// Visits every direct child declaration of `parent` (the AST Children() set).
		static void CollectChildrenFlat(Server::CompilationSession& session, const Scripting::AstNode& parent, uint32 sourceId, const std::string& container, const std::string& query, Core::Vector<WorkspaceSymbol>& out);

		// Visits a pre-collected member list (object/enum bodies expose members separately from
		// their generic Children() set). Matches ObjectDefinitionNode::GetBodyMembers()'s type.
		static void CollectMembersFlat(Server::CompilationSession& session, const Core::Vector<Ref<Scripting::AstNode>>& members, uint32 sourceId, const std::string& container, const std::string& query, Core::Vector<WorkspaceSymbol>& out);

		// Processes one declaration node: emits it when its name matches and recurses into any
		// nested scopes (namespace/object bodies) so a query finds members of non-matching parents.
		static void AddDeclarationFlat(Server::CompilationSession& session, const Scripting::AstNode& node, uint32 sourceId, const std::string& container, const std::string& query, Core::Vector<WorkspaceSymbol>& out);

		static void PushMatched(Core::Vector<WorkspaceSymbol>& out, Server::CompilationSession& session, const std::string& name, int kind, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end, const std::string& containerName);
	};
} // namespace C4LSP::Features
