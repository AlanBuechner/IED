#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// The lifecycle intrinsics, called like functions (docs/language/14, D-41). A program function of the
	/// same name wins: they are tried only when the name resolves to no function.
	///   take(x)            the value of x, leaving x zero
	///   destroy(p)         runs the destructor of the T at p (a T*); the storage is left as raw memory
	///   relocate(dst, src) moves the T at src into the raw storage at dst and leaves src zero
	bool IsLifecycleIntrinsic(const Core::String& name);
	QueryResult<Value> LowerLifecycleIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args);
} // namespace Scripting
