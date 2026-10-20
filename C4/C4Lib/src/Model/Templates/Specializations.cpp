#include "Model/Templates/Specializations.h"
#include "Model/Templates/PartialOrdering.h"
#include "Model/Templates/TemplateDeduction.h"
#include "Model/Templates/Packs.h"
#include "Model/TypeHandle.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include <string>

namespace Scripting
{
	Core::Vector<const ProgramModel::TemplateDef*> Specializations::InScope(const std::unordered_map<Core::String, Core::Vector<ProgramModel::TemplateDef>>& table, const Core::String& name,
		const Scope* scope)
	{
		Core::Vector<const ProgramModel::TemplateDef*> out;
		auto it = table.find(name);
		if (it == table.end()) return out;
		for (const ProgramModel::TemplateDef& d : it->second)
			if (d.declScope == scope) out.Push(&d);
		return out;
	}

	TemplatePatterns Specializations::PatternsOf(const ProgramModel::TemplateDef& spec)
	{
		return TemplatePatterns::OfArguments(spec.pattern, spec.params);
	}

	bool Specializations::SamePattern(const ProgramModel::TemplateDef& a, const ProgramModel::TemplateDef& b)
	{
		return PartialOrdering::AtLeastAsSpecialized(PatternsOf(a), PatternsOf(b)) && PartialOrdering::AtLeastAsSpecialized(PatternsOf(b), PatternsOf(a));
	}

	QueryResult<void> Specializations::CheckShape(const Core::String& name, const ProgramModel::TemplateDef& spec, const TemplateDefinitionNode& primary)
	{
		// A pack expansion only ends the list; the primary's fixed parameters each get an argument before it.
		const auto& arguments = spec.pattern->GetArguments();
		for (uint32 i = 0; i + 1 < arguments.Count(); ++i)
			if (dynamic_cast<const PackExpansionNode*>(arguments[i].Get()))
				return QueryResult<void>::Fail(QueryError::NotAValue, "in the specialization '" + Spelled(name, spec) +
					"' a pack expansion must be the last argument");
		const TemplatePatterns patterns = PatternsOf(spec);
		const bool variadic = PackIndex(primary) >= 0;
		const uint32 wanted = variadic ? FixedCount(primary) : primary.ParameterCount();
		const bool fits = variadic ? patterns.patterns.Count() >= wanted : patterns.patterns.Count() == wanted && !patterns.expansion;
		if (fits) return QueryResult<void>::Success();
		return QueryResult<void>::Fail(QueryError::NotAValue, "the specialization '" + Spelled(name, spec) + "' gives " +
			std::to_string(arguments.Count()) + " argument(s); the template '" + name + "' takes " + (variadic ? "at least " : "") +
			std::to_string(wanted));
	}

	Core::String Specializations::Spelled(const Core::String& name, const ProgramModel::TemplateDef& spec) { return name + spec.pattern->ToString(); }

	QueryResult<Specializations::Choice> Specializations::ForType(const ProgramModel& model, const ProgramModel::TemplateDef& primary, const Core::String& name,
		const TemplateArgs& args, uint32 unitId)
	{
		using Result = QueryResult<Choice>;
		struct Matched { const ProgramModel::TemplateDef* def; TemplateArgs args; };
		std::vector<Matched> matches;
		for (const ProgramModel::TemplateDef* spec : InScope(model.m_TypeSpecializations, name, primary.declScope))
		{
			if (auto shape = CheckShape(name, *spec, *primary.params); !shape.Ok()) return Result::Fail(shape.error, shape.message);
			if (auto own = Match(model, *spec, args, unitId); own.Ok()) matches.push_back({ spec, own.value });
		}
		if (matches.empty()) return Result::Success(Choice());

		// The most specialized match: one that no other match is more specialized than, and the only one.
		const Matched* best = nullptr;
		for (const Matched& m : matches)
		{
			bool dominated = false;
			for (const Matched& other : matches)
				dominated = dominated || (&other != &m && PartialOrdering::Compare(PatternsOf(*other.def), PatternsOf(*m.def)) > 0);
			if (dominated) continue;
			if (best && SamePattern(*best->def, *m.def))
			{
				const Core::String a = Spelled(name, *best->def), b = Spelled(name, *m.def);
				const bool byConst = a.find("const") != Core::String::npos || b.find("const") != Core::String::npos;
				return Result::Fail(QueryError::Ambiguous, "'" + a + "' and '" + b + "' are the same specialization of '" + name + "'" +
					(byConst ? " (a top-level const is not part of a type)" : "") + ": define it once");
			}
			if (best)
				return Result::Fail(QueryError::Ambiguous, "ambiguous specializations of '" + name + "': '" + Spelled(name, *best->def) +
					"' and '" + Spelled(name, *m.def) + "' both match and neither is more specialized");
			best = &m;
		}
		Choice choice;
		choice.def = best->def;
		choice.args = best->args;
		return Result::Success(choice);
	}

	QueryResult<const ProgramModel::TemplateDef*> Specializations::ForFunction(const ProgramModel& model, const ProgramModel::TemplateDef& primary, const Core::String& name,
		const TemplateArgs& args, uint32 unitId)
	{
		for (const ProgramModel::TemplateDef* spec : InScope(model.m_FunctionSpecializations, name, primary.declScope))
			if (Match(model, *spec, args, unitId).Ok()) return QueryResult<const ProgramModel::TemplateDef*>::Success(spec);
		return QueryResult<const ProgramModel::TemplateDef*>::Success(nullptr);
	}

	QueryResult<TemplateArgs> Specializations::Match(const ProgramModel& model, const ProgramModel::TemplateDef& spec, const TemplateArgs& args, uint32 unitId)
	{
		using Result = QueryResult<TemplateArgs>;
		const TemplatePatterns patterns = PatternsOf(spec);
		const uint32 fixed = patterns.patterns.Count();
		if (args.Count() < fixed || (!patterns.expansion && args.Count() != fixed)) return Result::Fail(QueryError::NotFound, "argument count");
		TemplateDeduction deduction(model, *spec.params, TemplateArgs());
		for (uint32 i = 0; i < args.Count(); ++i)
		{
			auto r = i < fixed ? deduction.FromTemplateArgument(patterns.patterns[i], args[i])
				: deduction.FromPackTemplateArgument(patterns.expansion, args[i], i - fixed);
			if (!r.Ok()) return Result::Fail(r.error, r.message);
		}
		Scope* binding = model.m_Symbols->CreateScope(Scope::Kind::Block, "specialization",
			const_cast<Scope*>(spec.declScope ? spec.declScope : model.m_Symbols->GetRoot()));
		const TemplateArgs own = deduction.Arguments();
		for (uint32 i = 0; i < FixedCount(*spec.params); ++i)
			if (i >= own.Count() || !own[i].IsSet()) return Result::Fail(QueryError::NotFound, "a parameter the pattern does not decide");
		TemplateArgumentResolver::BindAll(model, binding, *spec.params, own);
		for (uint32 i = 0; i < fixed; ++i)
			if (!Equal(model, patterns.patterns[i], args[i], unitId, binding)) return Result::Fail(QueryError::NotFound, "pattern mismatch");
		if (patterns.expansion && !ExpansionEqual(model, spec, args, fixed, unitId, binding)) return Result::Fail(QueryError::NotFound, "pattern mismatch");
		return Result::Success(own);
	}

	bool Specializations::ExpansionEqual(const ProgramModel& model, const ProgramModel::TemplateDef& spec, const TemplateArgs& args, uint32 from,
		uint32 unitId, Scope* binding)
	{
		const auto* expansion = static_cast<const PackExpansionNode*>(spec.pattern->GetArguments().Back().Get());
		const bool takesType = from < args.Count() ? args[from].IsType() : true;
		auto copies = Packs::ExpandArguments(model, *expansion, takesType, unitId, binding, nullptr);
		if (!copies.Ok() || copies.value.Count() != args.Count() - from) return false;
		for (uint32 i = from; i < args.Count(); ++i)
			if (!SameArgument(copies.value[i - from], args[i])) return false;
		return true;
	}

	bool Specializations::SameArgument(const TemplateArg& a, const TemplateArg& b)
	{
		if (a.IsType() != b.IsType()) return false;
		if (a.IsType()) return TypeHandle::UnwrapToValue(a.type) == TypeHandle::UnwrapToValue(b.type);
		return a == b;
	}

	bool Specializations::Equal(const ProgramModel& model, const AstNode* pattern, const TemplateArg& arg, uint32 unitId, Scope* binding)
	{
		auto resolved = TemplateArgumentResolver::ResolveWritten(model, arg.IsType(), pattern, unitId, binding, nullptr);
		return resolved.Ok() && SameArgument(resolved.value, arg);
	}
} // namespace Scripting
