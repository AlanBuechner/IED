#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// The optimizer's hints (docs/language/14 section 13): `likely(c)` and `unlikely(c)`, a bool the branch it decides
	/// usually goes one way on, and `mem_prefetch(p)`, the memory at a pointer about to be read, lowered to an
	/// IntrinsicOpNode. None changes what a program computes. A program function of the same name wins over the
	/// intrinsic.
	namespace HintIntrinsics
	{
		bool IsIntrinsic(const Core::String& name);
		QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args);
	}
} // namespace Scripting
