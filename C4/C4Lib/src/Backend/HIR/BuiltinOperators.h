#pragma once
#include "Core/Core.h"

namespace Scripting
{
	class Type;

	/// Which operands the built-in operators take (docs/language/10). A SIMD vector counts as its element type.
	namespace BuiltinOperators
	{
		/// Why the built-in binary operator `tok` cannot take a `l` and a `r`, or empty when it can. Pointer
		/// offsets (`p + n`) and null comparisons are already lowered by then.
		Core::String BinaryProblem(const Core::String& tok, const Type* l, const Type* r);

		/// Why the built-in prefix or postfix operator `tok` cannot take a `t`, or empty when it can.
		Core::String UnaryProblem(const Core::String& tok, const Type* t);

		/// The error for `l tok r` when an operand is an object and the left operand's type has no such
		/// operator (D-25: an operator is a method of its left operand's type).
		Core::String MissingOperator(const Core::String& tok, const Type* l, const Type* r);
	}
} // namespace Scripting
