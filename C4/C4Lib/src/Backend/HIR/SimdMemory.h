#pragma once
#include "Backend/HIR/SimdOperands.h"

namespace Scripting
{
	/// The SIMD intrinsics that move lanes to and from memory (D-62): `simd_load(p, N)`, `simd_store(p, v)`, their
	/// masked forms, and `simd_gather(p, indices)` / `simd_gather_masked(p, indices, mask)`. `p` points to the
	/// element type (an array decays to its first element) and needs only the element's alignment.
	namespace SimdMemory
	{
		SimdOperands::Result Lower(const SimdOperands::Call& call, SimdNode::Op op);
	}
} // namespace Scripting
