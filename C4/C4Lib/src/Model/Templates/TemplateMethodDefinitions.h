#pragma once
#include "Model/ProgramModel.h"

namespace Scripting
{
	// Out-of-line definitions of class template methods (`template<T> public void Box::Set(T x) { }`). Each must match
	// a method its template declares. These are the mismatches no template argument can repair, found before any
	// instance exists; one that depends on the arguments is found when an instance is made (ObjectTemplateBody).
	class TemplateMethodDefinitions
	{
	public:
		struct Mismatch
		{
			const FunctionDefinitionNode* definition = nullptr;
			Core::String why;
		};

		static Core::Vector<Mismatch> Unmatched(const ProgramModel& model);

		/// The scope declaring the class template `def` defines a method of (`Box` as a use written where the
		/// definition is would find it), or null when its qualifier names none.
		static const Scope* OwnerScope(const ProgramModel& model, const ProgramModel::TemplateMethodDef& def);

	private:
		static const Scope* QualifierScope(const ProgramModel& model, const ProgramModel::TemplateMethodDef& def);
		static bool DeclaresMemberTemplate(const ProgramModel& model, const ProgramModel::TemplateMethodDef& def);
		static Core::String Qualifier(const FunctionDefinitionNode& definition);
	};
} // namespace Scripting
