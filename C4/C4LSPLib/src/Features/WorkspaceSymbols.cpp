#include "Features/WorkspaceSymbols.h"
#include "Server/CompilationSession.h"
#include <algorithm>
#include <cctype>
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"

namespace C4LSP::Features
{
	// LSP SymbolKind values (same table DocumentSymbols uses).
	namespace Sym { constexpr int Namespace = 3; constexpr int Class = 5; constexpr int Enum = 10; constexpr int Function = 12; constexpr int Variable = 13; constexpr int Constant = 14; }

	// Cap so a large project (Bedrock + user code) cannot flood the client's symbol picker.
	static constexpr size_t kMaxResults = 500;

	namespace Detail
	{
		std::string ToLower(const std::string& text)
		{
			std::string result = text;
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		bool Matches(const std::string& name, const std::string& query)
		{
			if (query.empty())
				return true;
			return ToLower(name).find(ToLower(query)) != std::string::npos;
		}

		std::string JoinContainer(const std::string& outer, const std::string& inner)
		{
			return outer.empty() ? inner : (outer + " :: " + inner);
		}
	}

	void WorkspaceSymbols::PushMatched(Core::Vector<WorkspaceSymbol>& out, Server::CompilationSession& session, const std::string& name, int kind, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end, const std::string& containerName)
	{
		if (out.Count() >= kMaxResults || name.empty())
			return;
		WorkspaceSymbol symbol;
		symbol.name = name;
		symbol.kind = kind;
		symbol.location = session.ToEditorLocation(start, end);
		symbol.containerName = containerName;
		out.Push(symbol);
	}

	Core::Vector<WorkspaceSymbol> WorkspaceSymbols::ForQuery(Server::CompilationSession& session, const std::string& query)
	{
		auto& compiler = session.GetCompiler();
		const uint32 sourceCount = compiler.SourceCount();
		Core::Vector<WorkspaceSymbol> out;

		for (uint32 sourceId = 1; sourceId <= sourceCount && out.Count() < kMaxResults; ++sourceId)
		{
			if (!compiler.GetSourceManager().HasInfo(sourceId))
				continue;
			auto unit = compiler.GetCompilationUnit(sourceId);
			if (!unit || !unit->GetRootAST())
				continue;

			const std::string container; // file scope: no enclosing name
			CollectChildrenFlat(session, *unit->GetRootAST().Get(), sourceId, container, query, out);
		}

		return out;
	}

	void WorkspaceSymbols::CollectChildrenFlat(Server::CompilationSession& session, const Scripting::AstNode& parent, uint32 sourceId, const std::string& container, const std::string& query, Core::Vector<WorkspaceSymbol>& out)
	{
		auto children = parent.Children();
		for (const auto& child : children)
			if (child && child->startLocation.sourceId == sourceId)
				AddDeclarationFlat(session, *child.Get(), sourceId, container, query, out);
	}

	void WorkspaceSymbols::CollectMembersFlat(Server::CompilationSession& session, const Core::Vector<Ref<Scripting::AstNode>>& members, uint32 sourceId, const std::string& container, const std::string& query, Core::Vector<WorkspaceSymbol>& out)
	{
		for (const auto& member : members)
			if (member && member->startLocation.sourceId == sourceId)
				AddDeclarationFlat(session, *member.Get(), sourceId, container, query, out);
	}

	void WorkspaceSymbols::AddDeclarationFlat(Server::CompilationSession& session, const Scripting::AstNode& node, uint32 sourceId, const std::string& container, const std::string& query, Core::Vector<WorkspaceSymbol>& out)
	{
		switch (node.Type())
		{
		case Scripting::NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (!ns.IsRoot() && Detail::Matches(ns.Name(), query))
				PushMatched(out, session, ns.Name(), Sym::Namespace, ns.GetNameToken().GetStartLocation(), ns.GetNameToken().GetEndLocation(), container);

			// Keep descending even when the namespace itself did not match: a query like "Player"
			// must still find Player inside an unrelated namespace.
			const std::string inner = ns.IsRoot() ? container : Detail::JoinContainer(container, ns.Name());
			if (out.Count() < kMaxResults)
				CollectChildrenFlat(session, node, sourceId, inner, query, out);
			break;
		}

		case Scripting::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(node);
			const std::string inner = Detail::JoinContainer(container, obj.GetNameToken().GetText());

			// Emit the type when it matches, but ALWAYS descend into its body: a query for a
			// field name ("x") must find Point.x even though "Point" itself does not match.
			if (Detail::Matches(obj.GetNameToken().GetText(), query))
				PushMatched(out, session, obj.GetNameToken().GetText(), Sym::Class, obj.GetNameToken().GetStartLocation(), obj.GetNameToken().GetEndLocation(), container);

			if (out.Count() < kMaxResults)
				CollectMembersFlat(session, obj.GetBodyMembers(), sourceId, inner, query, out);
			break;
		}

		case Scripting::NodeType::EnumDefinition: {
			const auto& en = static_cast<const Scripting::EnumDefinitionNode&>(node);
			const std::string inner = Detail::JoinContainer(container, en.GetNameToken().GetText());

			if (Detail::Matches(en.GetNameToken().GetText(), query))
				PushMatched(out, session, en.GetNameToken().GetText(), Sym::Enum, en.GetNameToken().GetStartLocation(), en.GetNameToken().GetEndLocation(), container);

			// Enum values are themselves symbols; a value query ("green") must work even when the
			// enum name does not match. Each member is checked against the query individually.
			for (const auto& member : en.GetMembers())
				if (member && Detail::Matches(member->GetNameToken().GetText(), query))
					PushMatched(out, session, member->GetNameToken().GetText(), Sym::Constant, member->GetNameToken().GetStartLocation(), member->GetNameToken().GetEndLocation(), inner);
			break;
		}

		case Scripting::NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(node);
			if (!func.GetName())
				break;
			const auto& token = func.GetName()->GetLastIdentifierToken();
			std::string name = token.GetText();
			if (name.empty() || !Detail::Matches(name, query))
				break;
			// An out-of-line definition (`Counter::Next`) belongs to the type that qualifies it.
			std::string owner = container;
			const auto& segments = func.GetName()->GetSegments();
			for (uint32 i = 0; i + 1 < segments.Count(); ++i)
				owner = Detail::JoinContainer(owner, segments[i].nameToken.GetText());
			PushMatched(out, session, name, Sym::Function, token.GetStartLocation(), token.GetEndLocation(), owner);
			break;
		}

		case Scripting::NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(node);
			if (!Detail::Matches(var.GetNameToken().GetText(), query))
				break;
			PushMatched(out, session, var.GetNameToken().GetText(), Sym::Variable, var.GetNameToken().GetStartLocation(), var.GetNameToken().GetEndLocation(), container);
			break;
		}

		case Scripting::NodeType::UnionDefinition: {
			const auto& un = static_cast<const Scripting::UnionDefinitionNode&>(node);
			if (!Detail::Matches(un.GetNameToken().GetText(), query))
				break;
			PushMatched(out, session, un.GetNameToken().GetText(), Sym::Class, un.GetNameToken().GetStartLocation(), un.GetNameToken().GetEndLocation(), container);
			break;
		}

		case Scripting::NodeType::AliasDefinition: {
			const auto& alias = static_cast<const Scripting::AliasDefinitionNode&>(node);
			if (!Detail::Matches(alias.GetNameToken().GetText(), query))
				break;
			PushMatched(out, session, alias.GetNameToken().GetText(), Sym::Variable, alias.GetNameToken().GetStartLocation(), alias.GetNameToken().GetEndLocation(), container);
			break;
		}

		default:
			break; // statements and expressions are not workspace symbols
		}
	}
} // namespace C4LSP::Features
