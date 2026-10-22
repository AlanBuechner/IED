#include "Features/DocumentSymbols.h"
#include "Features/DeclarationText.h"
#include "Features/DocComment.h"
#include "Features/MacroUses.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include <algorithm>

namespace C4LSP::Features
{
	using Scripting::NodeType;

	Core::Vector<DocumentSymbol> DocumentSymbols::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		Core::Vector<DocumentSymbol> symbols;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return symbols;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (unit && unit->GetRootAST())
		{
			Walk walk{ session, sourceId };
			session.GetResolver().CollectNamespacePaths(walk.namespaces);
			CollectChildren(walk, *unit->GetRootAST().Get(), false, symbols);
		}
		AddMacros(session, uri, symbols);
		std::stable_sort(symbols.begin(), symbols.end(), [](const DocumentSymbol& a, const DocumentSymbol& b) {
			return a.range.start.line < b.range.start.line;
		});
		return symbols;
	}

	void DocumentSymbols::CollectChildren(Walk& walk, const Scripting::AstNode& parent, bool members, Core::Vector<DocumentSymbol>& out)
	{
		for (const auto& child : parent.Children())
			if (child && child->startLocation.sourceId == walk.sourceId) // skip inlined include trees and foreign nodes
				AddDeclaration(walk, *child.Get(), members, out);
	}

	void DocumentSymbols::AddDeclaration(Walk& walk, const Scripting::AstNode& node, bool member, Core::Vector<DocumentSymbol>& out)
	{
		switch (node.Type())
		{
		case NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (ns.IsRoot())
				return;
			DocumentSymbol symbol = MakeSymbol(walk, node, false);
			const std::string outer = walk.scope;
			walk.scope = outer.empty() ? ns.Name() : outer + "::" + ns.Name();
			CollectChildren(walk, node, false, symbol.children);
			walk.scope = outer;
			out.Push(symbol);
			break;
		}
		case NodeType::ObjectDefinition: {
			DocumentSymbol symbol = MakeSymbol(walk, node, member);
			for (const auto& body : static_cast<const Scripting::ObjectDefinitionNode&>(node).GetBodyMembers())
				if (body)
					AddDeclaration(walk, *body.Get(), true, symbol.children);
			out.Push(symbol);
			break;
		}
		case NodeType::EnumDefinition: {
			DocumentSymbol symbol = MakeSymbol(walk, node, member);
			for (const auto& value : static_cast<const Scripting::EnumDefinitionNode&>(node).GetMembers())
				if (value)
					symbol.children.Push(MakeSymbol(walk, *value.Get(), true));
			out.Push(symbol);
			break;
		}
		case NodeType::FunctionDefinition:
			if (!DeclarationText::NameOf(node).empty())
				out.Push(MakeSymbol(walk, node, member || DefinedForType(walk, node)));
			break;
		case NodeType::VariableDefinition:
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition:
			out.Push(MakeSymbol(walk, node, member));
			break;
		default:
			break; // statements and expressions are not document symbols
		}
	}

	DocumentSymbol DocumentSymbols::MakeSymbol(Walk& walk, const Scripting::AstNode& node, bool member)
	{
		DocumentSymbol symbol;
		symbol.name = DeclarationText::WrittenName(node);
		symbol.detail = DeclarationText::Detail(walk.session, node);
		symbol.kind = DeclarationText::SymbolKindOf(node, member);
		symbol.range = walk.session.ToEditorRange(walk.sourceId, DocComment::Start(walk.session, node), node.endLocation);
		const DeclarationText::NameLocation name = DeclarationText::NameLocationOf(node);
		symbol.selectionRange = name.found ? walk.session.ToEditorRange(walk.sourceId, name.simpleStart, name.end) : symbol.range;
		return symbol;
	}

	bool DocumentSymbols::DefinedForType(const Walk& walk, const Scripting::AstNode& node)
	{
		const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(node);
		if (!func.GetName() || func.GetName()->GetSegments().Count() < 2)
			return false;
		const std::string written = DeclarationText::WrittenName(node);
		const std::string qualifier = written.substr(0, written.rfind("::"));
		return walk.namespaces.count(qualifier) == 0 && walk.namespaces.count(walk.scope + "::" + qualifier) == 0;
	}

	void DocumentSymbols::AddMacros(Server::CompilationSession& session, const std::string& uri, Core::Vector<DocumentSymbol>& out)
	{
		for (const MacroUses::Definition& macro : MacroUses::DefinitionsIn(session.GetEditorText(uri)))
		{
			DocumentSymbol symbol;
			symbol.name = macro.name;
			symbol.detail = "macro";
			symbol.kind = SymbolKinds::Function;
			symbol.range = macro.range;
			symbol.selectionRange = macro.nameRange;
			out.Push(symbol);
		}
	}
} // namespace C4LSP::Features
