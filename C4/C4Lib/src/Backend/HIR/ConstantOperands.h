#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/Types/ArrayType.h"
#include <optional>

namespace Scripting
{
	// An operand written as a constant already shows a mistake that is undefined at run time: an integer division
	// by zero, a shift by the type's width or more, an index outside a fixed-size array. Each is an error.
	namespace ConstantOperands
	{
		/// The integer a lowered operand always is (`0`, `-1`, `(int64)4`, `N - 1`), when it is written as a constant
		/// or as a negation, `+`, `-` or `*` of constants (wrapping in the operation's type).
		std::optional<int64> IntegerOf(const HIRNode* node);
		/// Why `left op right` on the integer `type` is undefined, or empty.
		Core::String BinaryProblem(IntrinsicOp op, const Type* type, const HIRNode* right);
		/// Why `index` is outside `array`, or empty.
		Core::String IndexProblem(const ArrayType& array, const HIRNode* index);
	}
} // namespace Scripting
