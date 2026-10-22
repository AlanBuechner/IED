#include "Features/Hover.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Server/NodeLocator.h"
#include "Features/TypeText.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/Types.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace C4LSP::Features
{
	HoverResult Hover::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		HoverResult result;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return result;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit)
			return result;
		const auto& root = unit->GetRootAST();
		if (!root)
			return result;

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, root, position);
		if (!located.Found())
			return result;

		std::string text = BuildHoverText(session.GetResolver(), located);
		if (text.empty())
			return result;

		result.found = true;
		result.contents = "```c4\n" + text + "\n```";
		result.range = session.ToEditorRange(sourceId, located.node->startLocation, located.node->endLocation);
		return result;
	}

	std::string Hover::BuildHoverText(const Server::SemanticResolver& resolver, const Server::LocatedNode& located)
	{
		// Prefer a reference node (identifier / member access / call callee) with a resolved type.
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];

			if (node->Type() == Scripting::NodeType::IdentifierExpression ||
				node->Type() == Scripting::NodeType::MemberAccess ||
				node->Type() == Scripting::NodeType::StaticAccess)
			{
				Server::NodeResolution res = resolver.ResolveReference(*node, located.path);
				if (res.type || res.declaration)
					return BuildReferenceHover(*node, res.type, res.declaration);
			}

			// A type written in a declaration (`StringView` of `Init(StringView text)`) names that type, not the
			// declaration around it.
			if (node->Type() == Scripting::NodeType::QualType)
			{
				std::string typeText = BuildTypeHover(resolver, static_cast<const Scripting::QualTypeNode&>(*node), located.path);
				if (!typeText.empty())
					return typeText;
			}

			std::string declText = BuildDeclarationHover(*node);
			if (!declText.empty())
				return declText;
		}
		return "";
	}

	std::string Hover::BuildReferenceHover(const Scripting::AstNode& node, const Scripting::Type* type, const Scripting::AstNode* declaration)
	{
		if (declaration)
		{
			std::string declText = BuildDeclarationHover(*declaration);
			if (!declText.empty())
				return declText;
		}
		return TypeText(type);
	}

	std::string Hover::BuildTypeHover(const Server::SemanticResolver& resolver, const Scripting::QualTypeNode& type, const Core::Vector<const Scripting::AstNode*>& path)
	{
		const Scripting::Type* resolved = resolver.ResolveQualType(type, path);
		if (!resolved)
			return "";
		const Scripting::AstNode* declaration = DeclarationOf(NamedType(resolved));
		std::string declText = declaration ? BuildDeclarationHover(*declaration) : "";
		return declText.empty() ? std::string(TypeText(resolved)) : declText;
	}

	const Scripting::AstNode* Hover::DeclarationOf(const Scripting::Type* type)
	{
		if (const auto* object = dynamic_cast<const Scripting::ObjectType*>(type))
			return object->GetDeclNode();
		if (const auto* enumType = dynamic_cast<const Scripting::EnumType*>(type))
			return enumType->GetDeclNode();
		if (const auto* unionType = dynamic_cast<const Scripting::UnionType*>(type))
			return unionType->GetDeclNode();
		return nullptr;
	}

	std::string Hover::BuildDeclarationHover(const Scripting::AstNode& node)
	{
		switch (node.Type())
		{
		case Scripting::NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(node);
			if (!func.GetName())
				return "";
			std::string text = "function ";
			if (const auto* ret = func.GetReturnType().Get())
				text += ret->ToString() + " ";
			text += func.GetName()->GetLastIdentifierToken().GetText() + "(";
			const auto& params = func.GetParameters();
			for (uint32 i = 0; i < params.Count(); ++i)
			{
				if (i > 0)
					text += ", ";
				if (params[i]->GetType())
					text += params[i]->GetType()->ToString() + " ";
				text += params[i]->GetNameToken().GetText();
			}
			text += ")";
			return text;
		}
		case Scripting::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(node);
			std::string kind = "class";
			switch (obj.Kind())
			{
			case Scripting::ObjectKind::Struct:   kind = "struct"; break;
			case Scripting::ObjectKind::Interface: kind = "interface"; break;
			default: break;
			}
			return kind + " " + obj.GetNameToken().GetText();
		}
		case Scripting::NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(node);
			std::string text;
			if (var.GetType())
				text += var.GetType()->ToString() + " ";
			return text + var.GetNameToken().GetText();
		}
		case Scripting::NodeType::EnumDefinition: {
			const auto& en = static_cast<const Scripting::EnumDefinitionNode&>(node);
			return "enum " + en.GetNameToken().GetText();
		}
		case Scripting::NodeType::Parameter: {
			const auto& param = static_cast<const Scripting::ParameterNode&>(node);
			std::string text = "parameter ";
			if (param.GetType())
				text += param.GetType()->ToString() + " ";
			return text + param.GetNameToken().GetText();
		}
		default:
			return "";
		}
	}
} // namespace C4LSP::Features
