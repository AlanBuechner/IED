#include "Features/Hover.h"
#include "Features/DeclarationText.h"
#include "Features/DocComment.h"
#include "Features/MacroUses.h"
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
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace C4LSP::Features
{
	using Scripting::NodeType;

	static std::string CodeBlock(const std::string& code) { return "```c4\n" + code + "\n```"; }

	static bool Declares(const Scripting::AstNode& node)
	{
		switch (node.Type())
		{
		case NodeType::Namespace:
		case NodeType::ObjectDefinition:
		case NodeType::FunctionDefinition:
		case NodeType::VariableDefinition:
		case NodeType::EnumDefinition:
		case NodeType::EnumMember:
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition:
		case NodeType::Parameter:
			return true;
		default:
			return false;
		}
	}

	// What a parameter or a local is, said before its block: neither has an owner to qualify it with.
	static std::string KindLine(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		if (decl.Type() == NodeType::Parameter)
			return "parameter `" + DeclarationText::NameOf(decl) + "`\n\n";
		if (decl.Type() != NodeType::VariableDefinition)
			return "";
		const Core::Vector<const Scripting::AstNode*> path = DeclarationText::PathTo(session, decl);
		for (uint32 i = 0; i + 1 < path.Count(); ++i)
			if (path[i]->Type() == NodeType::FunctionDefinition)
				return "local `" + DeclarationText::NameOf(decl) + "`\n\n";
		return "";
	}

	HoverResult Hover::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		HoverResult result;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return result;

		const MacroUses::Use macro = MacroUses::At(session, uri, position);
		if (macro.found)
		{
			result.found = true;
			result.contents = MacroHover(macro.definition, macro.expansion, macro.doc);
			result.range = macro.range;
			return result;
		}

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return result;
		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return result;

		std::string text = BuildHoverText(session, located);
		if (text.empty())
			return result;

		result.found = true;
		result.contents = text;
		result.range = session.ToEditorRange(sourceId, located.node->startLocation, located.node->endLocation);
		return result;
	}

	std::string Hover::MacroHover(const std::string& definition, const std::string& expansion, const std::string& doc)
	{
		std::string text = CodeBlock(definition) + "\n\nExpands to:\n\n" + CodeBlock(expansion);
		return doc.empty() ? text : text + "\n\n" + doc;
	}

	std::string Hover::BuildHoverText(Server::CompilationSession& session, const Server::LocatedNode& located)
	{
		const Server::SemanticResolver& resolver = session.GetResolver();
		// Prefer a reference node (identifier / member access / call callee) with a resolved type.
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];

			if (node->Type() == NodeType::IdentifierExpression || node->Type() == NodeType::MemberAccess || node->Type() == NodeType::StaticAccess)
			{
				Server::NodeResolution res = resolver.ResolveReference(*node, located.path);
				if (res.type || res.declaration)
					return BuildReferenceHover(session, res.type, res.declaration);
			}

			// A type written in a declaration (`StringView` of `Init(StringView text)`) names that type, not the
			// declaration around it.
			if (node->Type() == NodeType::QualType)
			{
				std::string typeText = BuildTypeHover(session, static_cast<const Scripting::QualTypeNode&>(*node), located.path);
				if (!typeText.empty())
					return typeText;
			}

			if (Declares(*node))
			{
				std::string declText = DeclarationMarkdown(session, *node);
				if (!declText.empty())
					return declText;
			}
		}
		return "";
	}

	std::string Hover::BuildReferenceHover(Server::CompilationSession& session, const Scripting::Type* type, const Scripting::AstNode* declaration)
	{
		if (declaration)
		{
			std::string declText = DeclarationMarkdown(session, *declaration);
			if (!declText.empty())
				return declText;
		}
		const std::string typeText = TypeText(type);
		return typeText.empty() ? "" : CodeBlock(typeText);
	}

	std::string Hover::BuildTypeHover(Server::CompilationSession& session, const Scripting::QualTypeNode& type, const Core::Vector<const Scripting::AstNode*>& path)
	{
		const Scripting::Type* resolved = session.GetResolver().ResolveQualType(type, path);
		if (!resolved)
			return "";
		const Scripting::AstNode* declaration = DeclarationOf(NamedType(resolved));
		std::string declText = declaration ? DeclarationMarkdown(session, *declaration) : "";
		return declText.empty() ? CodeBlock(TypeText(resolved)) : declText;
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

	const Scripting::AstNode* Hover::Documented(Server::CompilationSession& session, const Scripting::AstNode& declaration)
	{
		if (declaration.Type() != NodeType::FunctionDefinition)
			return &declaration;
		std::vector<const Scripting::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(&declaration, declarations);
		for (const Scripting::AstNode* node : declarations)
			if (node && DeclarationText::WrittenName(*node).find("::") == std::string::npos)
				return node;
		return &declaration;
	}

	std::string Hover::DeclarationMarkdown(Server::CompilationSession& session, const Scripting::AstNode& declaration)
	{
		const Scripting::AstNode& shown = *Documented(session, declaration);
		const std::string code = DeclarationText::Qualified(session, shown);
		if (code.empty())
			return "";
		std::string text = KindLine(session, shown) + CodeBlock(code);
		std::string doc = DocComment::Of(session, shown);
		if (doc.empty() && &shown != &declaration)
			doc = DocComment::Of(session, declaration);
		return doc.empty() ? text : text + "\n\n" + doc;
	}
} // namespace C4LSP::Features
