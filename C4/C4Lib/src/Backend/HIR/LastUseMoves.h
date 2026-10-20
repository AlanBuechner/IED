#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// D-05's guarantee for locals (A10): a copy of a local's or by-value parameter's object that is its last use
	/// moves the object instead, when its address never escaped. Run on a function's finished HIR.
	///
	/// - A use is any read or write of the variable, in evaluation order; a use in the other arm of an `if` or
	///   `?:` does not follow it, and every use inside a loop follows a copy in that loop of a variable declared
	///   outside it. A use inside another cleanup (a `defer`) keeps every copy.
	/// - The address escapes when it is taken other than for the length of one expression (`&x`, a reference
	///   variable, a reference capture); a reference argument or a receiver does not count. A function with a
	///   `goto` moves nothing.
	/// - A moved variable gets a drop flag, set where the variable begins and cleared by the move; its cleanup
	///   runs only while the flag is set, so the moved object is destroyed once, by its new owner.
	void MoveAtLastUse(LoweringContext& ctx, HIRFunction& fn);
} // namespace Scripting
