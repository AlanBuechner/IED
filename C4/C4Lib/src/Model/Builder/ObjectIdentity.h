#pragma once
#include "Model/Types/ObjectType.h"
#include "Model/SymbolTable.h"

namespace Scripting
{
	struct ObjectDefinitionNode;

	/// Copy what a definition says about a type's identity onto it: class / struct / interface, and the
	/// namespaces and classes it is declared in (its runtime type name, docs/compiler/04).
	void ApplyDeclaredIdentity(ObjectType& type, const ObjectDefinitionNode& decl, const Scope* declScope);

	/// The same for a type declared in a function body: it is also named after its source position, so two
	/// local types never share a runtime name.
	void ApplyLocalIdentity(ObjectType& type, const ObjectDefinitionNode& decl, const Scope* declScope);

	/// The names around a type (class or enum) declared in a function body: the scopes' names, then the
	/// position of its name token.
	Core::Vector<Core::String> LocalEnclosingNames(const Scope* declScope, const SourceLocation& name);
} // namespace Scripting
