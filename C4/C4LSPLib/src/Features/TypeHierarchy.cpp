#include "Features/TypeHierarchy.h"
#include "Features/Implementation.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Parser/AstNode.h"
#include "Model/Types.h"
#include <algorithm>
#include <set>

namespace C4LSP::Features
{
	const Scripting::ObjectType* TypeHierarchy::TypeAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return nullptr;
		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return nullptr;
		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		const Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		return located.Found() && NamesAType(located) ? Implementation::TypeUnderCursor(session, located) : nullptr;
	}

	bool TypeHierarchy::NamesAType(const Server::LocatedNode& located)
	{
		for (uint32 i = located.path.Count(); i-- > 0;)
		{
			const Scripting::NodeType type = located.path[i]->Type();
			if (type == Scripting::NodeType::QualType)
				return true;
			if (type == Scripting::NodeType::ObjectDefinition)
				return i + 1 == located.path.Count(); // its own name, not somewhere in its body
		}
		return false;
	}

	Core::Vector<HierarchyItem> TypeHierarchy::Prepare(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		Core::Vector<HierarchyItem> items;
		const Scripting::ObjectType* type = TypeAt(session, uri, position);
		if (type && type->GetDeclNode())
			items.Push(HierarchyItem::Of(session, *type->GetDeclNode()));
		return items;
	}

	Core::Vector<HierarchyItem> TypeHierarchy::Supertypes(Server::CompilationSession& session, const HierarchyItem& item)
	{
		Core::Vector<HierarchyItem> items;
		const Scripting::ObjectType* type = TypeAt(session, item.uri, item.selectionRange.start);
		if (!type)
			return items;
		for (const Scripting::ObjectType* base : type->GetBaseTypes())
			if (base && base->GetDeclNode())
				items.Push(HierarchyItem::Of(session, *base->GetDeclNode()));
		return items;
	}

	Core::Vector<HierarchyItem> TypeHierarchy::Subtypes(Server::CompilationSession& session, const HierarchyItem& item)
	{
		Core::Vector<HierarchyItem> items;
		const Scripting::ObjectType* type = TypeAt(session, item.uri, item.selectionRange.start);
		if (!type)
			return items;
		std::vector<Server::SemanticResolver::ObjectDecl> types;
		session.GetResolver().CollectObjectTypes(types);
		std::set<const Scripting::AstNode*> seen;
		for (const auto& candidate : types)
		{
			if (!candidate.object || !candidate.decl || seen.count(candidate.decl))
				continue;
			const auto& bases = candidate.object->GetBaseTypes();
			if (std::find(bases.begin(), bases.end(), type) == bases.end())
				continue;
			seen.insert(candidate.decl);
			items.Push(HierarchyItem::Of(session, *candidate.decl));
		}
		return items;
	}
} // namespace C4LSP::Features
