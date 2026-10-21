#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// The integer bit intrinsics (docs/language/14 section 12): counting bits, rotating, swapping bytes and the high half
	/// of a product, on an integer or a packed integer vector, lowered to an IntrinsicOpNode. A program function of the
	/// same name wins over the intrinsic.
	namespace BitIntrinsics
	{
		bool IsIntrinsic(const Core::String& name);
		QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args);
	}
} // namespace Scripting
