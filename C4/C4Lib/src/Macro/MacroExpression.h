#pragma once
#include "Macro/MacroExpander.h"
#include "Core/Core.h"
#include <string>

namespace Scripting
{
	/// The value of a #if / #elif condition, or the error that stops it.
	struct ConditionValue
	{
		int64 value = 0;
		std::string error;

		bool Ok() const { return error.empty(); }
	};

	/// Evaluates a #if / #elif condition (docs/language/02 2.3): `defined X` / `defined(X)` first, then the
	/// macros expand, then C's integer constant expression over int64 (a name left after expansion is 0,
	/// `true` is 1). An operand that `&&`, `||` or `?:` skips is not evaluated, so `0 && 1 / 0` is 0.
	ConditionValue EvaluateCondition(const std::string& condition, const MacroTable& table, const ExpansionSite& site);
}
