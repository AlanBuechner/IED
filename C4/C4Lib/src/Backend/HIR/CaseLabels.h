#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// The labels of one switch (docs/language/09 4): each an integer, char or enum constant that the selector's
	/// type holds exactly, each named once. A scoped enum is matched only by its own members (D-32).
	class CaseLabels
	{
	public:
		CaseLabels(LoweringContext& ctx, const Type* selector) : m_Ctx(ctx), m_Selector(selector) {}

		/// Why a `selector` value cannot be switched on; empty when it can.
		static Core::String SelectorProblem(const Type* selector);

		/// The value of the case label `label`, or why it is not one.
		QueryResult<ConstantValue> Add(const AstNode* label);

	private:
		Core::String TypeProblem(const Type* label) const;
		Core::String RangeProblem(const ConstantValue& value, const Type* label) const;

		LoweringContext& m_Ctx;
		const Type* m_Selector;
		std::vector<int64> m_Values;
	};
} // namespace Scripting
