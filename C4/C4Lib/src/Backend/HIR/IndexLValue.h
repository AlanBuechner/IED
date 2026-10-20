#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// The lvalue of `base[index]` for a built-in array or pointer base: the address of element 0 plus a
	/// byte offset, dereferenced to the element. Shared by aggregate initialization and range-for.
	const LValue* BuildIndexLValue(LoweringContext& ctx, const Value& base, const HIRNode* indexNode,
		const Type* elemType, size_t elemSize);

	/// The array an element lvalue indexes (`a[i]` is a Deref of `&a + offset`), or null.
	const LValue* IndexedArray(const LValue* element);
} // namespace Scripting
