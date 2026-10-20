#include "Features/Implementation.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/TypeHandle.h"   // TypeHandle::UnwrapToValue (alias/reference -> value type)
#include "Model/Types.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Features/Definition.h"

namespace C4LSP::Features
{
	Core::Vector<Protocol::Location> Implementation::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
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

		const Scripting::ObjectType* target = TypeUnderCursor(session, located);
		if (!target)
			return {};

		// The model is program-wide and every unit shares it, so one scope-tree scan covers all user-defined
		// object types - no per-unit loop needed. Each type carries its declaring node (for the location).
		std::vector<Server::SemanticResolver::ObjectDecl> types;
		session.GetResolver().CollectObjectTypes(types);

		Core::Vector<Protocol::Location> out;
		std::set<const Scripting::ObjectType*> seen;
		for (const auto& t : types)
		{
			if (!t.object || !t.decl || seen.count(t.object))
				continue;
			std::set<const Scripting::ObjectType*> visited;
			if (DerivesFrom(t.object, target, visited))
			{
				seen.insert(t.object);
				out.Push(Definition::LocationForDeclaration(session, *t.decl));
			}
		}
		return out;
	}

	const Scripting::ObjectType* Implementation::TypeUnderCursor(Server::CompilationSession& session, const Server::LocatedNode& located)
	{
		const Server::SemanticResolver& resolver = session.GetResolver();

		// A type name in a declaration / cast / new: resolve the QualType through the model's own type verb.
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];
			if (node->Type() != Scripting::NodeType::QualType)
				continue;

			const auto& qt = static_cast<const Scripting::QualTypeNode&>(*node);
			if (!qt.BaseName())
				return nullptr;
			if (const Scripting::Type* type = resolver.ResolveQualType(qt, located.path))
				if (const Scripting::ObjectType* obj = dynamic_cast<const Scripting::ObjectType*>(Scripting::TypeHandle::UnwrapToValue(type)))
					return obj;
			return nullptr;
		}

		// The cursor is on a type definition itself: match the object whose declaring node is exactly this one.
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];
			if (node->Type() != Scripting::NodeType::ObjectDefinition &&
				node->Type() != Scripting::NodeType::EnumDefinition)
				continue;

			std::vector<Server::SemanticResolver::ObjectDecl> types;
			session.GetResolver().CollectObjectTypes(types);
			for (const auto& t : types)
				if (t.decl == node && t.object)
					return t.object;
		}

		return nullptr;
	}

	bool Implementation::DerivesFrom(const Scripting::ObjectType* type, const Scripting::ObjectType* target, std::set<const Scripting::ObjectType*>& visited)
	{
		if (!type || !target || type == target || visited.count(type))
			return false;
		visited.insert(type);

		for (const auto* base : type->GetBaseTypes())
		{
			if (base == target)
				return true;
			if (DerivesFrom(base, target, visited))
				return true;
		}
		return false;
	}
} // namespace C4LSP::Features
