#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	// The two branches of `c ? a : b` become one type (docs/language/10 6).
	namespace ConditionalBranches
	{
		/// The branches' type when they have the same one, the usual arithmetic result of two numbers (an enum
		/// counts as its underlying integer), or the type one branch converts to (a base object or pointer, `null`
		/// to a pointer); otherwise an error naming both.
		QueryResult<const Type*> CommonType(LoweringContext& ctx, const Value& a, const Value& b);
		/// `branch` as a value of `type`: an object is a new object (a copy of a named one, docs/language/06).
		QueryResult<const HIRNode*> BranchValue(LoweringContext& ctx, const Value& branch, const Type* type);
	}
} // namespace Scripting
