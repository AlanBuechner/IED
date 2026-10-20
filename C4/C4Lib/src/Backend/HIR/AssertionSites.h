#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// A test assertion (`assert_eq(...)`) reports its own call site (B37): the call becomes the runtime's `_at`
	/// form with the file and line of `site` appended, after the arguments the caller wrote. Any other call is
	/// left as it is.
	void LocateAssertion(LoweringContext& ctx, CallNode* call, const AstNode* site);
}
