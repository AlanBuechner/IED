#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// What a call that returns `returned` yields: a `T&` return is the location at the address the callee returns
	/// (read, assigned and bound like any lvalue: `f() = 9`, `int32& r = *it;`), anything else the call's value.
	Value CallResult(LoweringContext& ctx, const HIRNode* call, const Type* returned);
} // namespace Scripting
