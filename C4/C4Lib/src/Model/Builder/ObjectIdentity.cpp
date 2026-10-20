#include "Model/Builder/ObjectIdentity.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"

namespace Scripting
{
	namespace
	{
		ObjectType::ObjectKind KindOf(ObjectKind declared)
		{
			switch (declared)
			{
			case ObjectKind::Struct:    return ObjectType::ObjectKind::Struct;
			case ObjectKind::Interface: return ObjectType::ObjectKind::Interface;
			default:                    return ObjectType::ObjectKind::Class;
			}
		}

	}

	Core::Vector<Core::String> LocalEnclosingNames(const Scope* declScope, const SourceLocation& name)
	{
		Core::Vector<Core::String> names = EnclosingNames(declScope);
		names.Push("__local_" + std::to_string(name.sourceId) + "_" + std::to_string(name.line) + "_" + std::to_string(name.column));
		return names;
	}

	void ApplyDeclaredIdentity(ObjectType& type, const ObjectDefinitionNode& decl, const Scope* declScope)
	{
		type.SetObjectKind(KindOf(decl.Kind()));
		type.SetDeclNode(&decl);
		type.SetEnclosingNames(EnclosingNames(declScope));
	}

	void ApplyLocalIdentity(ObjectType& type, const ObjectDefinitionNode& decl, const Scope* declScope)
	{
		type.SetObjectKind(KindOf(decl.Kind()));
		type.SetDeclNode(&decl);
		type.SetEnclosingNames(LocalEnclosingNames(declScope, decl.GetNameToken().GetStartLocation()));
	}
} // namespace Scripting
