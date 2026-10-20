#pragma once
#include "Model/ProgramModel.h"
#include "Model/Templates/TemplateArguments.h"

namespace Scripting
{
	struct ObjectDefinitionNode;
	struct EnumDefinitionNode;

	/// Types declared in a class template's body (BUG-179). Each instance has its own: a nested struct or enum
	/// is built with the instance's bindings (it may name `T`), a nested template is registered for that
	/// instance, and every one is named in the instance's class layer (`Outer<float>::Inner`).
	class NestedTypes
	{
	public:
		/// The instance being built: its type, its bindings (the layer `T` is bound in and what a member
		/// type's methods lower with) and its class layer.
		struct Owner
		{
			const ObjectType* type = nullptr;
			const InstantiationBindings* bindings = nullptr;
			Scope* binding = nullptr;
			Scope* scope = nullptr;
			uint32 unitId = 0;
		};

		static QueryResult<void> AddObject(const ProgramModel& model, const Owner& owner, const ObjectDefinitionNode& nested);
		static QueryResult<void> AddEnum(const ProgramModel& model, const Owner& owner, const EnumDefinitionNode& nested);

	private:
		static void Register(const Owner& owner, const Core::String& name, const SymbolInfo& symbol);
		static SymbolInfo TypeSymbol(const Type* type);
		/// The names a member type is declared in: its owner instance, written as a type (`Ns::Outer<float>`).
		static Core::Vector<Core::String> EnclosingNames(const Owner& owner);
	};
} // namespace Scripting
