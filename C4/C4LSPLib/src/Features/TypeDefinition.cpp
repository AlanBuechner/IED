#include "Features/TypeDefinition.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Features/Definition.h"
#include "Features/TypeText.h"
#include "Model/Types.h"
#include <cctype>

namespace C4LSP::Features
{
	Core::Vector<Protocol::Location> TypeDefinition::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {};

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return {};

		const Server::SemanticResolver& resolver = session.GetResolver();

		// A variable or parameter's own name: the type it is declared with.
		auto declared = DeclaredType(session, sourceId, located, position);
		if (!declared.Empty())
			return declared;

		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];

			// A value reference whose resolved type is a user-defined object/enum: jump to that type.
			if (node->Type() == Scripting::NodeType::IdentifierExpression ||
				node->Type() == Scripting::NodeType::MemberAccess)
			{
				auto locations = ResolveReference(session, resolver, *node, located.path);
				if (!locations.Empty())
					return locations;
			}

			// A type name (in a declaration, cast, `new`, or qualified path): jump to the type definition.
			if (node->Type() == Scripting::NodeType::QualType)
			{
				auto locations = ResolveTypeName(session, *node, located.path);
				if (!locations.Empty())
					return locations;
			}
		}
		return {};
	}

	Core::Vector<Protocol::Location> TypeDefinition::ResolveReference(Server::CompilationSession& session, const Server::SemanticResolver& resolver, const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path)
	{
		return LocationOfType(session, resolver.ResolveReference(node, path).type);
	}

	Core::Vector<Protocol::Location> TypeDefinition::LocationOfType(Server::CompilationSession& session, const Scripting::Type* type)
	{
		// Only user-defined named types have a definition to jump to: a `Point*` or a `Point&` still lands on Point.
		const Scripting::AstNode* typeDecl = DeclarationOfNamedType(type);
		if (!typeDecl)
			return {};
		return { session.ToEditorLocation(typeDecl->startLocation, typeDecl->endLocation) };
	}

	Core::Vector<Protocol::Location> TypeDefinition::DeclaredType(Server::CompilationSession& session, uint32 sourceId, const Server::LocatedNode& located, const Protocol::Position& position)
	{
		const Scripting::AstNode* declaration = Definition::DeclarationNamedAt(session, sourceId, located, position);
		const Scripting::QualTypeNode* type = nullptr;
		if (declaration && declaration->Type() == Scripting::NodeType::VariableDefinition)
			type = static_cast<const Scripting::VariableDefinitionNode&>(*declaration).GetType().Get();
		else if (declaration && declaration->Type() == Scripting::NodeType::Parameter)
			type = static_cast<const Scripting::ParameterNode&>(*declaration).GetType().Get();
		return type ? LocationOfType(session, session.GetResolver().ResolveQualType(*type, located.path)) : Core::Vector<Protocol::Location>();
	}

	Core::Vector<Protocol::Location> TypeDefinition::ResolveTypeName(Server::CompilationSession& session, const Scripting::AstNode& typeNode, const Core::Vector<const Scripting::AstNode*>& path)
	{
		const auto& qt = static_cast<const Scripting::QualTypeNode&>(typeNode);
		if (!qt.BaseName())
			return {};

		std::string full; // outermost..innermost, for a qualified lookup
		std::string simple; // the last identifier segment (a type definition is keyed by it)
		for (const auto& seg : qt.BaseName()->GetSegments())
			if (seg.nameToken.GetType() == Scripting::TokenType::Identifier)
			{
				full += (full.empty() ? "" : "::") + std::string(seg.nameToken.GetText());
				simple = seg.nameToken.GetText();
			}
		if (simple.empty())
			return {};

		// Prefer the model's own qualified resolution: it lands on exactly the type this spelling names,
		// with its declaring node. A nested type (`Vec2` inside `Test`) doesn't resolve through the global
		// qualified walk, so fall back to a by-name scan of every declaration when that finds nothing.
		const Server::SemanticResolver& resolver = session.GetResolver();
		if (const Scripting::SymbolInfo* info = resolver.Lookup(full, 0))
			if (info->kind == Scripting::SymbolKind::Type && info->declNode)
				return { session.ToEditorLocation(info->declNode->startLocation, info->declNode->endLocation) };

		std::vector<const Scripting::AstNode*> decls;
		resolver.CollectDeclarationsByName(simple, decls);
		Core::Vector<Protocol::Location> result;
		for (const Scripting::AstNode* decl : decls)
		{
			if (decl->Type() != Scripting::NodeType::ObjectDefinition &&
				decl->Type() != Scripting::NodeType::EnumDefinition &&
				decl->Type() != Scripting::NodeType::UnionDefinition &&
				decl->Type() != Scripting::NodeType::AliasDefinition)
				continue; // a type reference wants a type definition, not a same-named function/variable
			result.Push(session.ToEditorLocation(decl->startLocation, decl->endLocation));
		}
		return result;
	}
} // namespace C4LSP::Features
