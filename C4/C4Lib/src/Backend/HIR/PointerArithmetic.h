#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// C pointer arithmetic (docs/language/10): `pointer + n`, `n + pointer` and `pointer - n` move by n
	/// elements (bytes for a void*); `pointer - pointer` is the int64 number of elements between two
	/// pointers to one type. `op` is "+" or "-"; the operands are already lowered.
	QueryResult<Value> PointerArithmetic(LoweringContext& ctx, const Core::String& op, const Value& lhs, const Value& rhs);

	/// Whether `op` on operands of these types is pointer arithmetic.
	bool IsPointerArithmetic(const Core::String& op, const Type* left, const Type* right);
} // namespace Scripting
