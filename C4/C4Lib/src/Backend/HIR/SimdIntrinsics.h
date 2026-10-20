#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// The SIMD intrinsics (D-62, docs/language/14 section 11): `simd_*` calls on `packed T[N]` values lowered to a
	/// SimdNode. A program function of the same name wins over the intrinsic.
	namespace SimdIntrinsics
	{
		bool IsIntrinsic(const Core::String& name);
		QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args);
	}
} // namespace Scripting
