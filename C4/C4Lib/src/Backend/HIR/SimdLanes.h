#pragma once
#include "Backend/HIR/SimdOperands.h"

namespace Scripting
{
	/// The SIMD intrinsics that rearrange lanes (D-62): `simd_shuffle(a, i...)` / `simd_shuffle(a, b, i...)` with
	/// constant indices, `simd_concat(a, b)` and `simd_splat(x, N)`.
	namespace SimdLanes
	{
		SimdOperands::Result LowerShuffle(const SimdOperands::Call& call);
		SimdOperands::Result LowerConcat(const SimdOperands::Call& call);
		SimdOperands::Result LowerSplat(const SimdOperands::Call& call);
	}
} // namespace Scripting
