#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// The atomic intrinsics (D-60, docs/language/14 10): `atomic_load`, `atomic_store`, `atomic_exchange`,
	/// `atomic_compare_exchange` (and `_weak`), `atomic_fetch_add` / `_sub` / `_and` / `_or` / `_xor` on a location
	/// reached through a pointer, and `atomic_fence`, each with a constant `MemoryOrder`. A program function of the
	/// same name wins over the intrinsic.
	namespace Atomics
	{
		bool IsIntrinsic(const Core::String& name);
		QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args);
	}
} // namespace Scripting
