#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// A call through a funcptr or a func (`f(args)` where `f` holds one): the arguments are checked against the
	/// signature's parameter count and converted to its parameter types; a reference parameter receives its
	/// argument's address. `args` are the loaded argument rvalues and `values` the original argument values
	/// (lvalues kept), in source order; a braced list has a null rvalue.
	QueryResult<Value> LowerIndirectCall(LoweringContext& ctx, const AstNode* site, const HIRNode* target, const SignatureType* fp,
		const std::vector<const HIRNode*>& args, const std::vector<Value>& values);
} // namespace Scripting
