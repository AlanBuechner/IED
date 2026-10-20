#pragma once
#include "Model/ProgramModel.h"
#include "Model/OverloadResolution.h"
#include "Model/Templates/TemplateArguments.h"

namespace Scripting
{
	/// A call that may name function templates: what QueryFunction knows about it.
	struct TemplateCall
	{
		Core::String name;                                  // as written, possibly qualified (`Math::Max`)
		const Type* receiver = nullptr;                     // a member call's object: selects extension templates
		const TemplateArgumentListNode* written = nullptr;  // explicit arguments (`Max<int32>`), or null
		const Core::Vector<const Type*>* argTypes = nullptr; // the call's argument types (null entry: a braced list)
		const Type* returnType = nullptr;                   // what a target signature returns (D-17), for no call
		uint32 unitId = 0;
		const Scope* scope = nullptr;                       // where the call is
	};

	/// Function templates as members of overload sets (docs/language/08, D-17, D-18, D-21).
	class FunctionTemplates
	{
	public:
		/// Adds a candidate for every template the call can name whose arguments are all found - written,
		/// deduced from the argument types (and from the receiver, for an extension template) or defaulted -
		/// instantiated for them. A template that cannot be a candidate adds why to `reasons`.
		static void Collect(const ProgramModel& model, const TemplateCall& call, Core::Vector<OverloadCandidate>& out,
			Core::Vector<Core::String>& reasons);
		/// The same for the member templates of `owner` and its bases (BUG-117): a call on an `owner` object, or an
		/// unqualified call in one of its methods.
		static void CollectMembers(const ProgramModel& model, const TemplateCall& call, const ObjectType* owner,
			Core::Vector<OverloadCandidate>& out, Core::Vector<Core::String>& reasons);

		/// For a new object-template instance: every extension template whose receiver it is (`extends Box<T>`
		/// for a `Box<int32>`) and whose arguments the receiver decides, instantiated and attached as the
		/// instance's extension method, so member and operator lookups find it like any other.
		static void AttachExtensions(const ProgramModel& model, const ObjectType* instance, uint32 unitId);

		/// `call.name` written as a value without template arguments, when it names function templates (D-17):
		/// a TemplateNameType that finds the instance a target signature asks for; null when it names none.
		static const TemplateNameType* NameOf(const ProgramModel& model, const TemplateCall& call);
		/// The instance of the templates `call` names whose signature is exactly `target`'s, its arguments
		/// deduced from that signature's parameter and return types (`funcptr<int32(int32)> f = Identity;`).
		static QueryResult<const FunctionRecord*> ForTarget(const ProgramModel& model, TemplateCall call, const SignatureType& target);

	private:
		static Core::Vector<const ProgramModel::TemplateDef*> Visible(const ProgramModel& model, const TemplateCall& call);
		static void CollectFrom(const ProgramModel& model, const TemplateCall& call, const Core::Vector<const ProgramModel::TemplateDef*>& defs,
			Core::Vector<OverloadCandidate>& out, Core::Vector<Core::String>& reasons);
		/// The templates a body-less declaration's definition may be among: its owner's members, or the free ones.
		static Core::Vector<const ProgramModel::TemplateDef*> Peers(const ProgramModel& model, const ProgramModel::TemplateDef& decl, const Core::String& name);
		static QueryResult<const FunctionRecord*> Instantiate(const ProgramModel& model, const ProgramModel::TemplateDef& def,
			const Core::String& name, const TemplateCall& call);
		static QueryResult<TemplateArgs> Deduce(const ProgramModel& model, const ProgramModel::TemplateDef& def,
			const Core::String& name, const TemplateCall& call);
		static QueryResult<const FunctionRecord*> Make(const ProgramModel& model, const ProgramModel::TemplateDef& def,
			const Core::String& name, TemplateArgs& args, const TemplateCall& call);
		static bool CouldDefine(const ProgramModel::TemplateDef& def, const ProgramModel::TemplateDef& decl);
		static const FunctionRecord* DefinitionOf(const ProgramModel& model, const ProgramModel::TemplateDef& decl,
			const Core::String& name, const TemplateArgs& args, const FunctionRecord* declared, const TemplateCall& call);
		static QueryResult<const Type*> ExtendedType(const ProgramModel& model, const ProgramModel::TemplateDef& def,
			Scope* binding, const Type* receiver, uint32 unitId);
		static void AttachAsMethod(const ProgramModel& model, const ObjectType* instance, const FunctionRecord* rec,
			const FunctionDefinitionNode& fn);
	};
} // namespace Scripting
