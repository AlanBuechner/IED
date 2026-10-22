#include "Features/WorkspaceSymbols.h"
#include "Features/DeclarationText.h"
#include "Features/MacroUses.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include <algorithm>
#include <cctype>
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"

namespace C4LSP::Features
{
	using Scripting::NodeType;

	// Cap so a large project (Bedrock + user code) cannot flood the client's symbol picker.
	static constexpr size_t kMaxResults = 500;

	static std::string ToLower(const std::string& text)
	{
		std::string result = text;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	static bool Matches(const std::string& name, const std::string& query)
	{
		return query.empty() || ToLower(name).find(ToLower(query)) != std::string::npos;
	}

	static std::string Joined(const std::string& outer, const std::string& inner)
	{
		return outer.empty() ? inner : (inner.empty() ? outer : outer + "::" + inner);
	}

	Core::Vector<WorkspaceSymbol> WorkspaceSymbols::ForQuery(Server::CompilationSession& session, const std::string& query)
	{
		Walk walk{ session, query };
		session.GetResolver().CollectNamespacePaths(walk.namespaces);
		auto& compiler = session.GetCompiler();
		for (uint32 sourceId = 1; sourceId <= compiler.SourceCount() && walk.out.Count() < kMaxResults; ++sourceId)
		{
			if (!compiler.GetSourceManager().HasInfo(sourceId))
				continue;
			auto unit = compiler.GetCompilationUnit(sourceId);
			if (unit && unit->GetRootAST())
				for (const auto& child : unit->GetRootAST()->Children())
					if (child && child->startLocation.sourceId == sourceId)
						AddDeclaration(walk, *child.Get(), sourceId, "", false);
		}
		AddMacros(walk);
		return walk.out;
	}

	void WorkspaceSymbols::AddMacros(Walk& walk)
	{
		for (uint32 sourceId = 1; sourceId <= walk.session.GetCompiler().SourceCount() && walk.out.Count() < kMaxResults; ++sourceId)
		{
			const std::string_view text = walk.session.RawTextOf(sourceId);
			if (text.find("define") == std::string_view::npos)
				continue; // most files define none
			const std::string uri = walk.session.UriForSourceId(sourceId);
			if (uri.empty())
				continue;
			for (const MacroUses::Definition& macro : MacroUses::DefinitionsIn(text))
				if (walk.out.Count() < kMaxResults && Matches(macro.name, walk.query))
					walk.out.Push(WorkspaceSymbol{ macro.name, SymbolKinds::Function, Protocol::Location{ uri, macro.nameRange }, "" });
		}
	}

	void WorkspaceSymbols::AddEach(Walk& walk, const Core::Vector<Ref<Scripting::AstNode>>& nodes, uint32 sourceId, const std::string& container, bool member)
	{
		for (const auto& node : nodes)
			if (node && node->startLocation.sourceId == sourceId && walk.out.Count() < kMaxResults)
				AddDeclaration(walk, *node.Get(), sourceId, container, member);
	}

	void WorkspaceSymbols::AddDeclaration(Walk& walk, const Scripting::AstNode& node, uint32 sourceId, const std::string& container, bool member)
	{
		switch (node.Type())
		{
		case NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (!ns.IsRoot())
				Push(walk, node, container, false);
			// Keep descending when the namespace did not match: "Player" must still find Player inside it.
			const std::string inner = ns.IsRoot() ? container : Joined(container, ns.Name());
			for (const auto& child : node.Children())
				if (child && child->startLocation.sourceId == sourceId && walk.out.Count() < kMaxResults)
					AddDeclaration(walk, *child.Get(), sourceId, inner, false);
			break;
		}
		case NodeType::ObjectDefinition: {
			Push(walk, node, container, member);
			// Always descend: a query for a field name ("x") must find Point.x though "Point" does not match.
			AddEach(walk, static_cast<const Scripting::ObjectDefinitionNode&>(node).GetBodyMembers(), sourceId, Joined(container, DeclarationText::NameOf(node)), true);
			break;
		}
		case NodeType::EnumDefinition: {
			Push(walk, node, container, member);
			const std::string inner = Joined(container, DeclarationText::NameOf(node));
			for (const auto& value : static_cast<const Scripting::EnumDefinitionNode&>(node).GetMembers())
				if (value)
					Push(walk, *value.Get(), inner, true);
			break;
		}
		case NodeType::FunctionDefinition: {
			// An out-of-line definition (`Counter::Next`) belongs to what qualifies it: a type, unless it names a namespace.
			const std::string written = DeclarationText::WrittenName(node);
			const size_t qualified = written.rfind("::");
			if (qualified == std::string::npos)
			{
				Push(walk, node, container, member);
				break;
			}
			const std::string owner = Joined(container, written.substr(0, qualified));
			Push(walk, node, owner, member || walk.namespaces.count(owner) == 0);
			break;
		}
		case NodeType::VariableDefinition:
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition:
			Push(walk, node, container, member);
			break;
		default:
			break; // statements and expressions are not workspace symbols
		}
	}

	void WorkspaceSymbols::Push(Walk& walk, const Scripting::AstNode& node, const std::string& container, bool member)
	{
		const std::string name = DeclarationText::NameOf(node);
		if (walk.out.Count() >= kMaxResults || name.empty() || !Matches(name, walk.query))
			return;
		const DeclarationText::NameLocation location = DeclarationText::NameLocationOf(node);
		WorkspaceSymbol symbol;
		symbol.name = name;
		symbol.kind = DeclarationText::SymbolKindOf(node, member);
		symbol.location = location.found ? walk.session.ToEditorLocation(location.simpleStart, location.end) : walk.session.ToEditorLocation(node.startLocation, node.endLocation);
		symbol.containerName = container;
		walk.out.Push(symbol);
	}
} // namespace C4LSP::Features
