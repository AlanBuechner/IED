#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	class ExprLowerer;
	struct VaArgExpressionNode;

	/// A C variadic function's argument list (D-35, docs/language/07 7.7): a `va_list` variable that
	/// `va_start(ap)` opens in a variadic function, `va_arg(ap, T)` reads from, `va_copy(dst, src)` copies and
	/// `va_end(ap)` closes. A program function of the same name wins over the intrinsics.
	namespace VarArgs
	{
		bool IsIntrinsic(const Core::String& name);
		QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args);
		QueryResult<Value> LowerArgument(LoweringContext& ctx, ExprLowerer& expr, const VaArgExpressionNode& node);
	}
} // namespace Scripting
