#include "Features/TypeDefinition.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
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
		const Server::NodeResolution res = resolver.ResolveReference(node, path);
		if (!res.type)
			return {};

		// Only user-defined named types have a definition to jump to. Unwrap alias/reference/pointer
		// indirection to the underlying value type (a `Point* p` reference still navigates to Point).
		const Scripting::Type* value = res.type;
		while (value)
		{
			switch (value->GetKind())
			{
			case Scripting::TypeKind::Alias:
				value = static_cast<const Scripting::AliasType*>(value)->GetAliasedType();
				break;
			case Scripting::TypeKind::Reference:
				value = static_cast<const Scripting::ReferenceType*>(value)->GetReferentType();
				break;
			case Scripting::TypeKind::Pointer:
				value = static_cast<const Scripting::PointerType*>(value)->GetPointeeType();
				break;
			default:
				break; // a named / scalar value type
			}
		}
		if (!value)
			return {};

		const Scripting::AstNode* typeDecl = nullptr;
		switch (value->GetKind())
		{
		case Scripting::TypeKind::Object:
			typeDecl = static_cast<const Scripting::ObjectType*>(value)->GetDeclNode();
			break;
		case Scripting::TypeKind::Enum:
			typeDecl = static_cast<const Scripting::EnumType*>(value)->GetDeclNode();
			break;
		default:
			break; // scalars / pointers to scalars have no type definition to navigate to
		}

		if (!typeDecl)
			return {};
		return { session.ToEditorLocation(typeDecl->startLocation, typeDecl->endLocation) };
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
