#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// The implicit conversions (docs/language/04 4.9, D-32) for every place a value becomes a typed location:
	/// an initialization, an argument, an assignment, a return.
	namespace ImplicitConversion
	{
		/// `value` (an rvalue) as a `target` that is not an object or an array: numbers convert to numbers, a
		/// plain enum from and to integers, an array to its element pointer, the constant 0 to a pointer (as
		/// null), a pointer to bool (its truth value). Pointer-to-pointer rules are CheckPointerConversion's;
		/// a callable target is ConvertCallable's. The failure says why, without naming the context.
		QueryResult<const HIRNode*> Scalar(LoweringContext& ctx, const HIRNode* value, const Type* target);

		/// Why a `from` value cannot become a `to` object or array, with the explicit form to write; empty when
		/// it can (the same type, or a derived object sliced to its base).
		Core::String ObjectProblem(const Type* from, const Type* to);

		/// A warning when converting `value` (written as `site`) to `target` may change it (D-32, B22): to a narrower
		/// integer, a floating-point value to an integer, a double to a float. A constant that fits says nothing.
		void WarnIfNarrowing(LoweringContext& ctx, const AstNode* site, const Value& value, const Type* target);

		/// Why a `type` value cannot be a condition (an `if`, a loop, `?:`, an operand of `&&` / `||`): a
		/// condition is a bool, a number, an enum or a pointer (09 2). Empty when it can.
		Core::String ConditionProblem(LoweringContext& ctx, const Type* type);

		/// Whether `value` is the integer constant 0: the one integer that becomes a pointer (null), in a conversion or
		/// beside a pointer in `==` / `!=`. A bool's `false` is not one.
		bool IsZeroConstant(const HIRNode* value);
	}
} // namespace Scripting
