#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"
#include <set>

namespace C4Compiler { class AstNode; class ProgramModel; class ObjectType; class SymbolTable; }
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
		static const C4Compiler::ObjectType* TypeUnderCursor(Server::CompilationSession& session, const Server::LocatedNode& located);

	private:
		// The methods of the types derived from a method's type that override it: same name, same parameter count.
		static Core::Vector<Protocol::Location> Overrides(Server::CompilationSession& session, const C4Compiler::AstNode& method);
		// True when `type` derives from `target` directly or transitively.
		static bool DerivesFrom(const C4Compiler::ObjectType* type, const C4Compiler::ObjectType* target, std::set<const C4Compiler::ObjectType*>& visited);
	};
} // namespace C4LSP::Features
