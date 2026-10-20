#include "Backend/HIR/CaseLabels.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Model/ConstEvaluator.h"
#include "Model/IntegerRanges.h"
#include "Model/TypeHandle.h"
#include "Model/Types/EnumType.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<ConstantValue>;

		bool Scoped(const Type* t)
		{
			const auto* e = dynamic_cast<const EnumType*>(t);
			return e && e->IsScoped();
		}

		// The integer type a switch compares: an enum's underlying type.
		const Type* IntegerOf(const Type* t)
		{
			const auto* e = dynamic_cast<const EnumType*>(t);
			return e && e->GetUnderlyingType() ? e->GetUnderlyingType() : t;
		}

	}

	Core::String CaseLabels::SelectorProblem(const Type* selector)
	{
		if (selector && !selector->IsNormalized() && (selector->IsIntegral() || selector->GetKind() == TypeKind::Enum)) return "";
		return "a switch selects on an integer, a char or an enum, not a '" + Core::String(selector ? selector->GetName() : "void") + "'";
	}

	Result CaseLabels::Add(const AstNode* label)
	{
		ConstEvaluator eval(*m_Ctx.queries().Model(), m_Ctx.scope);
		auto value = eval.Evaluate(label, m_Ctx.unitId);
		if (!value.Ok()) return Result::Fail(QueryError::NotAValue, "switch case value is not a constant: " + value.message);
		auto typed = ExprLowerer(m_Ctx).ResolveExpr(label);
		if (!typed.Ok()) return Result::Fail(typed.error, typed.message);
		const Type* type = TypeHandle::UnwrapToValue(typed.value.type);
		if (Core::String why = TypeProblem(type); !why.empty()) return Result::Fail(QueryError::NotAValue, why);
		if (Core::String why = RangeProblem(value.value, type); !why.empty()) return Result::Fail(QueryError::NotAValue, why);
		const int64 bits = value.value.AsInt();
		if (std::find(m_Values.begin(), m_Values.end(), bits) != m_Values.end())
			return Result::Fail(QueryError::NotAValue, "'case " + label->ToString() + "' is already a case of this switch");
		m_Values.push_back(bits);
		return value;
	}

	Core::String CaseLabels::TypeProblem(const Type* label) const
	{
		if (!label || label->IsNormalized() || (!label->IsIntegral() && label->GetKind() != TypeKind::Enum))
			return "a case label is an integer, a char or an enum constant, not a '" + Core::String(label ? label->GetName() : "void") + "'";
		if (label == m_Selector || (!Scoped(m_Selector) && !Scoped(label))) return "";
		if (Scoped(m_Selector))
			return "the cases of a switch on the enum class '" + m_Selector->GetName() + "' are its members, not a '" + label->GetName() + "'";
		return "'" + label->GetName() + "' is an enum class: its members are cases only of a switch on a '" + label->GetName() + "'";
	}

	Core::String CaseLabels::RangeProblem(const ConstantValue& value, const Type* label) const
	{
		const Type* target = IntegerOf(m_Selector);
		const int64 v = value.AsInt();
		const bool negative = v < 0 && !IntegerRanges::IsUnsigned(IntegerOf(label));
		const bool fits = negative ? IntegerRanges::FitsNegative(v, target) : static_cast<uint64>(v) <= IntegerRanges::MaxOf(target);
		if (fits) return "";
		const Core::String text = negative ? std::to_string(v) : std::to_string(static_cast<uint64>(v));
		return "case value " + text + " does not fit in the switch's '" + m_Selector->GetName() + "'";
	}
} // namespace Scripting
