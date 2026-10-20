#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// The value a new object of type `target` starts from (docs/language/06): a copy of an lvalue or of a
	/// view of an existing object, a fresh object relocated as is, the base sub-object of a derived object, a
	/// scalar converted by the implicit conversions (docs/language/04 4.9). Initialization, by-value arguments,
	/// returns of non-locals, aggregate elements and thrown values use it. A failure names both types.
	QueryResult<const HIRNode*> InitialValue(LoweringContext& ctx, const Value& source, const Type* target);

	/// The same conversion, with a failure that gives only the reason (the caller words the context).
	QueryResult<const HIRNode*> ConvertedValue(LoweringContext& ctx, const Value& source, const Type* target);

	/// One argument bound to its parameter: a reference parameter receives the argument's address (moved to
	/// the base sub-object for a derived argument; a fresh object that needs destruction becomes a statement
	/// temporary, a plain rvalue is materialized by codegen); anything else is the InitialValue of the
	/// parameter. `loaded` is the argument's rvalue (after any null-literal retyping or init-list build).
	QueryResult<const HIRNode*> BindArgument(LoweringContext& ctx, const Value& arg, const HIRNode* loaded, const Type* param);
} // namespace Scripting
