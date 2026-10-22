#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"
#include <set>

namespace Scripting { class AstNode; class ProgramModel; class ObjectType; class SymbolTable; }
namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Go-to-implementation: from a type name, jump to every type that derives from it (directly
	/// or transitively). This is the "implementations of an interface" view in VS. The cursor must
	/// be on a type name (a class/struct/interface declaration or use).
	class Implementation
	{
	public:
		static Core::Vector<Protocol::Location> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		// The ObjectType under the cursor (null when not on a type name).
		static const Scripting::ObjectType* TypeUnderCursor(Server::CompilationSession& session, const Server::LocatedNode& located);

	private:
		// True when `type` derives from `target` directly or transitively.
		static bool DerivesFrom(const Scripting::ObjectType* type, const Scripting::ObjectType* target, std::set<const Scripting::ObjectType*>& visited);
	};
} // namespace C4LSP::Features
