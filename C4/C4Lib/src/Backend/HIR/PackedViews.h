#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// A name read as one of an object's packed views (docs/language/11): its array view (`data`, the whole
	/// vector as a `packed T[N]`), one lane (`x`) or a run of lanes (`xz`), as an lvalue at `base`. Null when the
	/// name is not a view of `obj`. Member access (`v.xy`) and a bare name in a method (`xy`) both read views here.
	const LValue* PackedViewLValue(LoweringContext& ctx, const ObjectType* obj, const LValue* base, const Core::String& name);
} // namespace Scripting
