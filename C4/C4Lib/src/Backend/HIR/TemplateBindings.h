#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// Code made from a template sees its parameters (T := int32, N := 4, Ts := int32, float) as names in the
	/// current lowering layer. A name already registered there keeps its first meaning.
	void BindTemplateParameters(LoweringContext& ctx, const InstantiationBindings* bindings);
} // namespace Scripting
