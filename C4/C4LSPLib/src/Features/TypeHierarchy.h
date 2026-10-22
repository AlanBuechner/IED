#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Features/HierarchyItem.h"
#include <string>

namespace Scripting { class ObjectType; }

namespace C4LSP::Server { class CompilationSession; struct LocatedNode; }

namespace C4LSP::Features
{
	/// Type hierarchy: the class, struct or interface at a position, the types it derives from, and those that derive
	/// from it directly.
	class TypeHierarchy
	{
	public:
		/// The type named at `position` (its declaration or a use); empty for anything else.
		static Core::Vector<HierarchyItem> Prepare(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		static Core::Vector<HierarchyItem> Supertypes(Server::CompilationSession& session, const HierarchyItem& item);
		static Core::Vector<HierarchyItem> Subtypes(Server::CompilationSession& session, const HierarchyItem& item);

	private:
		static const Scripting::ObjectType* TypeAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		/// Whether the position is on a type: one written (a QualType), or a class's own name - not in its body.
		static bool NamesAType(const Server::LocatedNode& located);
	};
} // namespace C4LSP::Features
