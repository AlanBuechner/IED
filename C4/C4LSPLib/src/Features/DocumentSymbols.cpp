#include "Features/DocumentSymbols.h"
#include "Server/CompilationSession.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace C4LSP::Features
{
	// LSP SymbolKind values.
	namespace Sym { constexpr int File = 6; constexpr int Module = 9; constexpr int Namespace = 3; constexpr int Class = 5; constexpr int Method = 6; constexpr int Property = 7; constexpr int Field = 8; constexpr int Constructor = 4; constexpr int Enum = 10; constexpr int Constant = 14; constexpr int Function = 12; constexpr int Variable = 13; }

	Core::Vector<DocumentSymbol> DocumentSymbols::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {};

		Core::Vector<DocumentSymbol> symbols;
		CollectChildren(session, *unit->GetRootAST().Get(), sourceId, symbols);
		return symbols;
	}

	void DocumentSymbols::CollectChildren(Server::CompilationSession& session, const Scripting::AstNode& parent, uint32 sourceId, Core::Vector<DocumentSymbol>& out)
	{
		auto children = parent.Children();
		for (const auto& child : children)
		{
			if (!child || child->startLocation.sourceId != sourceId)
				continue; // skip inlined include trees and foreign nodes
			AddDeclaration(session, *child.Get(), out);
		}
	}

	void DocumentSymbols::AddDeclaration(Server::CompilationSession& session, const Scripting::AstNode& node, Core::Vector<DocumentSymbol>& out)
	{
		switch (node.Type())
		{
		case Scripting::NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (ns.IsRoot())
				return;
			DocumentSymbol symbol = MakeSymbol(session, node, ns.Name(), "namespace", Sym::Namespace,
				ns.GetNameToken().GetStartLocation(), ns.GetNameToken().GetEndLocation());
			CollectChildren(session, node, node.startLocation.sourceId, symbol.children);
			out.Push(symbol);
			break;
		}
		case Scripting::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(node);
			std::string kindText = obj.Kind() == Scripting::ObjectKind::Struct ? "struct" : (obj.Kind() == Scripting::ObjectKind::Interface ? "interface" : "class");
			DocumentSymbol symbol = MakeSymbol(session, node, obj.GetNameToken().GetText(), kindText, Sym::Class,
				obj.GetNameToken().GetStartLocation(), obj.GetNameToken().GetEndLocation());
			for (const auto& member : obj.GetBodyMembers())
			{
				if (member)
					AddDeclaration(session, *member.Get(), symbol.children);
			}
			out.Push(symbol);
			break;
		}
		case Scripting::NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(node);
			std::string name = func.GetName() ? func.GetName()->GetFirstIdentifierToken().GetText() : "";
			if (name.empty())
				return;
			DocumentSymbol symbol = MakeSymbol(session, node, name, BuildFunctionDetail(func), Sym::Function,
				func.GetName()->startLocation, func.GetName()->endLocation);
			out.Push(symbol);
			break;
		}
		case Scripting::NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(node);
			DocumentSymbol symbol = MakeSymbol(session, node, var.GetNameToken().GetText(),
				var.GetType() ? var.GetType()->ToString() : "", Sym::Variable,
				var.GetNameToken().GetStartLocation(), var.GetNameToken().GetEndLocation());
			out.Push(symbol);
			break;
		}
		case Scripting::NodeType::EnumDefinition: {
			const auto& en = static_cast<const Scripting::EnumDefinitionNode&>(node);
			DocumentSymbol symbol = MakeSymbol(session, node, en.GetNameToken().GetText(), "enum", Sym::Enum,
				en.GetNameToken().GetStartLocation(), en.GetNameToken().GetEndLocation());
			for (const auto& member : en.GetMembers())
			{
				if (!member)
					continue;
				DocumentSymbol m = MakeSymbol(session, *member.Get(), member->GetNameToken().GetText(), "enum member", Sym::Constant,
					member->GetNameToken().GetStartLocation(), member->GetNameToken().GetEndLocation());
				symbol.children.Push(m);
			}
			out.Push(symbol);
			break;
		}
		case Scripting::NodeType::UnionDefinition: {
			const auto& un = static_cast<const Scripting::UnionDefinitionNode&>(node);
			DocumentSymbol symbol = MakeSymbol(session, node, un.GetNameToken().GetText(), "union", Sym::Class,
				un.GetNameToken().GetStartLocation(), un.GetNameToken().GetEndLocation());
			out.Push(symbol);
			break;
		}
		case Scripting::NodeType::AliasDefinition: {
			const auto& alias = static_cast<const Scripting::AliasDefinitionNode&>(node);
			DocumentSymbol symbol = MakeSymbol(session, node, alias.GetNameToken().GetText(), "alias", Sym::Variable,
				alias.GetNameToken().GetStartLocation(), alias.GetNameToken().GetEndLocation());
			out.Push(symbol);
			break;
		}
		default:
			break; // statements and expressions are not document symbols
		}
	}

	DocumentSymbol DocumentSymbols::MakeSymbol(Server::CompilationSession& session, const Scripting::AstNode& node, const std::string& name, const std::string& detail, int kind, const Scripting::SourceLocation& selectionStart, const Scripting::SourceLocation& selectionEnd)
	{
		uint32 sourceId = node.startLocation.sourceId;
		DocumentSymbol symbol;
		symbol.name = name;
		symbol.detail = detail;
		symbol.kind = kind;
		symbol.range = session.ToEditorRange(sourceId, node.startLocation, node.endLocation);
		symbol.selectionRange = session.ToEditorRange(sourceId, selectionStart, selectionEnd);
		return symbol;
	}

	std::string DocumentSymbols::BuildFunctionDetail(const Scripting::FunctionDefinitionNode& func)
	{
		std::string detail;
		if (const auto* ret = func.GetReturnType().Get())
			detail += ret->ToString() + " ";
		detail += "(";
		const auto& params = func.GetParameters();
		for (uint32 i = 0; i < params.Count(); ++i)
		{
			if (i > 0)
				detail += ", ";
			if (params[i]->GetType())
				detail += params[i]->GetType()->ToString() + " ";
			detail += params[i]->GetNameToken().GetText();
		}
		detail += ")";
		return detail;
	}
} // namespace C4LSP::Features
