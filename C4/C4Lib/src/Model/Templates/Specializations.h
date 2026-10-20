#pragma once
#include "Model/ProgramModel.h"
#include "Model/Templates/TemplateArguments.h"
#include "Model/Templates/PartialOrdering.h"

namespace Scripting
{
	/// The specialization a template's instance uses (docs/language/08 7, D-20).
	class Specializations
	{
	public:
		struct Choice
		{
			const ProgramModel::TemplateDef* def = nullptr; // the specialization; null for the primary template
			TemplateArgs args;                              // the specialization's own parameters' arguments
		};

		/// For the complete arguments `args` of the type template `primary`: the most specialized of its
		/// specializations whose pattern they match (C++ partial ordering). Two matches neither more
		/// specialized than the other are an error naming both.
		static QueryResult<Choice> ForType(const ProgramModel& model, const ProgramModel::TemplateDef& primary, const Core::String& name,
			const TemplateArgs& args, uint32 unitId);

		/// The explicit specialization of the function template `primary` for `args`, or null.
		static QueryResult<const ProgramModel::TemplateDef*> ForFunction(const ProgramModel& model, const ProgramModel::TemplateDef& primary,
			const Core::String& name, const TemplateArgs& args, uint32 unitId);

	private:
		/// The arguments of `spec`'s own parameters that make its pattern exactly `args`; failure when none do.
		static QueryResult<TemplateArgs> Match(const ProgramModel& model, const ProgramModel::TemplateDef& spec, const TemplateArgs& args,
			uint32 unitId);
		static bool Equal(const ProgramModel& model, const AstNode* pattern, const TemplateArg& arg, uint32 unitId, Scope* binding);
		/// Whether the trailing expansion of `spec`'s pattern stands for exactly `args` from `from` on.
		static bool ExpansionEqual(const ProgramModel& model, const ProgramModel::TemplateDef& spec, const TemplateArgs& args, uint32 from,
			uint32 unitId, Scope* binding);
		static bool SameArgument(const TemplateArg& a, const TemplateArg& b);
		/// An error when `spec` gives the wrong number of arguments for `primary`, or an expansion before its last.
		static QueryResult<void> CheckShape(const Core::String& name, const ProgramModel::TemplateDef& spec, const TemplateDefinitionNode& primary);
		static Core::Vector<const ProgramModel::TemplateDef*> InScope(const std::unordered_map<Core::String, Core::Vector<ProgramModel::TemplateDef>>& table,
			const Core::String& name, const Scope* scope);
		static TemplatePatterns PatternsOf(const ProgramModel::TemplateDef& spec);
		/// Whether two specializations' patterns match exactly the same arguments.
		static bool SamePattern(const ProgramModel::TemplateDef& a, const ProgramModel::TemplateDef& b);
		static Core::String Spelled(const Core::String& name, const ProgramModel::TemplateDef& spec);
	};
} // namespace Scripting
