#pragma once
#include "Model/ProgramModel.h"
#include "Model/Templates/TemplateArguments.h"

namespace Scripting
{
	struct AliasDefinitionNode;
	struct VariableDefinitionNode;
	struct UnionDefinitionNode;
	struct FunctionDefinitionNode;
	struct Declarator;

	/// Where an instance's members are declared: the layer that binds the template's parameters, the
	/// instance's own class layer (its static data members; its members' emitted symbols are named from it),
	/// and the asking unit.
	struct InstanceSite
	{
		Scope* binding = nullptr;
		Scope* scope = nullptr;
		uint32 unitId = 0;
		const InstantiationBindings* owner = nullptr;   // a member type's: the enclosing instance's bindings
	};

	/// Builds an object-template instance's members from the template's body (docs/language/08), every
	/// type resolved in the instance's binding layer (T := int32): its bases, aliases, fields, packed lane
	/// regions and methods, each method with its lowering record. A method declared in the body without a
	/// body gets the out-of-line definition with the same name and parameter types (BUG-014); that
	/// definition may name the template's parameters differently (`template<U> void Box<U>::Set(U v)`).
	class ObjectTemplateBody
	{
	public:
		ObjectTemplateBody(const ProgramModel& model, ObjectType& instance, const ProgramModel::TemplateDef& def,
			const Core::String& templateName, const TemplateArgs& args, const InstanceSite& site)
			: m_Model(model), m_Instance(instance), m_Def(def), m_Name(templateName), m_Args(args), m_Binding(site.binding),
			  m_Scope(site.scope), m_UnitId(site.unitId), m_Owner(site.owner) {}

		QueryResult<void> Build();

		/// The header naming the class template's parameters in an out-of-line definition.
		static const TemplateDefinitionNode* ClassParameters(const FunctionDefinitionNode& definition);

	private:
		QueryResult<void> AddBases();
		QueryResult<void> AddMember(const AstNode* member);
		QueryResult<void> AddAlias(const AliasDefinitionNode& alias);
		QueryResult<void> AddFields(const VariableDefinitionNode& var);
		/// A static data member: one object per instance, in the instance's class layer; an initializer that
		/// folds in the instance's bindings (`static const int64 Size = sizeof(T);`) is its starting value.
		void AddStatic(const VariableDefinitionNode& var, const Declarator& d, const Type* type);
		QueryResult<void> AddPackedRegion(const UnionDefinitionNode& un);
		QueryResult<void> AddMethod(const FunctionDefinitionNode& fn);
		/// A struct, class or enum declared in the body: one per instance (BUG-179).
		QueryResult<void> AddNestedType(const AstNode* member);
		/// What this instance's members lower with: its parameters' arguments (or, for a member type, its
		/// owner's) and its own name.
		InstantiationBindings OwnBindings() const;

		/// The out-of-line definition of `method` whose parameters resolve to `parameters`, with the binding
		/// layer its own template parameter names resolve in (null when there is none).
		struct OutOfLine { const FunctionDefinitionNode* fn = nullptr; Scope* binding = nullptr; };
		OutOfLine FindOutOfLine(const Core::String& method, const Core::Vector<const Type*>& parameters, bool isConst) const;
		bool Defines(const ProgramModel::TemplateMethodDef& def) const;
		/// Whether an out-of-line definition's qualifier (`Box<T>::` / `Box<T*>::`) names this definition.
		bool QualifierFits(const FunctionDefinitionNode& definition) const;
		Scope* BindingFor(const FunctionDefinitionNode& definition) const;
		bool SameParameters(const FunctionDefinitionNode& definition, Scope* binding, const Core::Vector<const Type*>& parameters) const;
		void RecordOwnBindings(const FunctionRecord* rec, const FunctionDefinitionNode& definition) const;
		/// The out-of-line definitions of this instance's member templates (`template<T> template<U> R S<T>::F(U u)`).
		void AddMemberTemplateDefinitions() const;
		/// Every out-of-line definition of this template's methods was taken by a declaration of this instance.
		QueryResult<void> CheckDefinitionsTaken() const;
		bool Takes(const FunctionDefinitionNode& definition) const;

		const ProgramModel& m_Model;
		ObjectType& m_Instance;
		const ProgramModel::TemplateDef& m_Def;
		const Core::String& m_Name;
		const TemplateArgs& m_Args;
		Scope* m_Binding;
		Scope* m_Scope;
		uint32 m_UnitId;
		const InstantiationBindings* m_Owner;
	};
} // namespace Scripting
