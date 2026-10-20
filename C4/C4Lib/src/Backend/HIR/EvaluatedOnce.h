#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <vector>

namespace Scripting
{
	/// `location` with every value it is computed from (a pointer, an index, an indexer's receiver) evaluated once
	/// into temporaries by `steps`, so a read and then a write of the result run nothing twice: `f().x += 1` calls `f`
	/// once, `a[i++] += 1` steps `i` once (BUG-201). A slot, a global, `this` and fields of them are returned as they are.
	const LValue* EvaluatedOnce(LoweringContext& ctx, const LValue* location, std::vector<const HIRNode*>& steps);

	/// `result` after `steps`, as one value; `result` itself when there are none.
	const HIRNode* AfterSteps(LoweringContext& ctx, const std::vector<const HIRNode*>& steps, const HIRNode* result);
}
