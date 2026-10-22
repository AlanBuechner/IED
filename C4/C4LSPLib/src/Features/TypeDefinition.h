#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"

namespace Scripting { class AstNode; class Type; }
namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	/// Go-to-type-definition: from a type name (in a declaration, cast, `new`, member access base,
	/// or qualified path) jump to where that type is defined. Resolves value references and bare
	/// type names through the shared SemanticResolver (the compiler's own query verbs).
	class TypeDefinition
	{
	public:
		static Core::Vector<Protocol::Location> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

	private:
		static Core::Vector<Protocol::Location> ResolveReference(Server::CompilationSession& session, const Server::SemanticResolver& resolver, const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path);
		/// Where the named type a value of `type` has is defined (through aliases, references, pointers and arrays).
		static Core::Vector<Protocol::Location> LocationOfType(Server::CompilationSession& session, const Scripting::Type* type);
		/// The type a variable or parameter named at `position` is declared with (BUG-350).
		static Core::Vector<Protocol::Location> DeclaredType(Server::CompilationSession& session, uint32 sourceId, const Server::LocatedNode& located, const Protocol::Position& position);
		static Core::Vector<Protocol::Location> ResolveTypeName(Server::CompilationSession& session, const Scripting::AstNode& typeNode, const Core::Vector<const Scripting::AstNode*>& path);
	};
} // namespace C4LSP::Features
