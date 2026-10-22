#pragma once
#include "Model/ProgramModel.h"
#include "Model/Templates/TemplateArguments.h"

namespace Scripting
{
	/// Turns a template and its arguments into an ordinary type or function record (docs/language/08,
	/// compiler/02). The requester never knows a template exists: it asks for `Vec<int32>` and gets an
	/// ordinary class back. Parameters are bound in a scope layer of fake aliases (T := int32, N := 4); no
	/// AST is ever rewritten. Results are cached by definition and resolved arguments, so two spellings of
	/// one instantiation share one instance.
	class TemplateInstantiator
	{
	public:
		/// The type template `name` found from `declScope`, for the arguments a use writes. The arguments resolve
		/// in `outerBinding` (the enclosing instantiation's bindings, or null), then from `argContext`, the scope
		/// the use is written in; the instance's body sees its parameters, then the template's declaring scope.
		/// The result is layout-complete.
		QueryResult<const Type*> InstantiateTypeTemplate(const ProgramModel& model, const Core::String& name,
			const Scope* declScope, const TemplateArgumentListNode& args, uint32 unitId, Scope* outerBinding, const Scope* argContext) const;

		/// Where a type declared in a class template's body is made for one instance (BUG-179): the instance's
		/// bindings, which the member type's methods lower with, and the names it is declared in.
		struct MemberTypeSite { InstantiationBindings ownerBindings; Core::Vector<Core::String> enclosing; uint32 unitId = 0; };
		/// That member type, for `def` sitting on the instance (`declScope` its class layer, `bindingParent` its
		/// bindings). The result is layout-complete.
		QueryResult<const Type*> InstantiateMemberType(const ProgramModel& model, const ProgramModel::TemplateDef& def,
			const Core::String& name, const MemberTypeSite& site) const;

		/// The function template `def` for complete arguments already bound in `binding`: its signature and
		/// lowering record. An extension template's record has `receiver` (the extended type) as receiver.
		QueryResult<const FunctionRecord*> InstantiateFunction(const ProgramModel& model, const ProgramModel::TemplateDef& def,
			const Core::String& name, const TemplateArgs& args, Scope* binding, const Type* receiver, uint32 unitId) const;
		/// The instance of the function template `def` (not an extension) for complete `args` made before, or null.
		const FunctionRecord* MadeFunction(const ProgramModel& model, const ProgramModel::TemplateDef& def, const Core::String& name,
			const TemplateArgs& args) const;

		/// The type template `name` a use in `declScope` names: the one declared innermost on its lookup chain.
		const ProgramModel::TemplateDef* ChooseTypeDefinition(const ProgramModel& model, const Core::String& name, const Scope* declScope) const;

	private:
		static Core::String FunctionKey(const Core::String& name, const ProgramModel::TemplateDef& def, const TemplateArgs& args, const Type* receiver);
		static Scope* BodyParent(const ProgramModel& model, const ProgramModel::TemplateDef& def);
		static bool DeclaredIn(const ProgramModel::TemplateDef& def, const Scope* s);
		/// `def` is the definition whose body builds the instance (the primary or a specialization) and `args`
		/// its parameters' arguments; `instanceArgs` are the instance's own (`Box<int32*>`).
		QueryResult<const Type*> InstantiateObject(const ProgramModel& model, const ProgramModel::TemplateDef& def, const Core::String& name,
			const Core::String& key, const TemplateArgs& instanceArgs, const TemplateArgs& args, Scope* binding, uint32 unitId) const;
		/// An instance's signature, and for each of its parameters the declared parameter it comes from (a
		/// parameter pack is several).
		struct Signature { const FunctionType* type = nullptr; std::vector<uint32> source; };
		QueryResult<Signature> FunctionSignature(const ProgramModel& model, const FunctionDefinitionNode& fn, Scope* binding, uint32 unitId) const;
		/// The class layer of a new object instance.
		static Scope* InstanceScope(const ProgramModel& model, const ObjectType& instance, const ProgramModel::TemplateDef& def,
			const Core::String& name);
		/// A member template instance's bindings plus the names its out-of-line definition gives its owner's parameters.
		static InstantiationBindings WithOwnerNames(InstantiationBindings own, const InstantiationBindings& ownerNames);
		static void RecordParameterPacks(FunctionRecord& rec, const FunctionDefinitionNode& fn, const Signature& signature);
	};
} // namespace Scripting
