#pragma once
#include "Model/ProgramModel.h"

namespace Scripting
{
	/// Function templates declared in a class (BUG-117: `struct P { template<U> static int32 F(U u); }`). Each is a
	/// template of its owner type, found through a receiver (`p.F(1)`), a qualifying type (`P::F(1)`,
	/// `S<int32>::F(1)`) or the enclosing type (`F(1)` in a method of P); its instances are methods of the owner.
	class MemberTemplates
	{
	public:
		/// `fn`, written in the body of `owner` whose class layer is `scope`; `binding` binds a class template
		/// instance's parameters for it (null for a plain class).
		static QueryResult<void> Declare(const ProgramModel& model, const ObjectType* owner, const FunctionDefinitionNode& fn,
			const Scope* scope, Scope* binding);
		/// An out-of-line definition of a member template of `owner` (`template<U> R P::F(U u) { ... }`); `binding`
		/// and `ownerNames` bind the names the definition gives a class template's parameters.
		static void Define(const ProgramModel& model, const ObjectType* owner, const FunctionDefinitionNode& definition, Scope* binding,
			const InstantiationBindings& ownerNames);
		/// Attaches every out-of-line definition of a plain class's member templates; runs once all files are registered.
		static void AttachDefinitions(const ProgramModel& model);
		/// Whether `owner` itself declares a member template named `name`.
		static bool Declares(const ProgramModel& model, const ObjectType* owner, const Core::String& name);
		/// The member templates named `name` of `type` and its bases, the most derived first.
		static Core::Vector<const ProgramModel::TemplateDef*> Named(const ProgramModel& model, const ObjectType* type, const Core::String& name);

	private:
		static Core::String NameOf(const FunctionDefinitionNode& fn);
	};
} // namespace Scripting
