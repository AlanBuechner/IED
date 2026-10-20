#pragma once
#include "Backend/HIR/LoweringContext.h"
#include <functional>

namespace Scripting
{
	/// The bounds of a loop over element indices. Each call builds a fresh int64 rvalue of its bound, so a bound
	/// may be read more than once (a constant, or a load of the slot holding a run-time count).
	struct IndexRange
	{
		std::function<const HIRNode*()> begin;
		std::function<const HIRNode*()> end;
	};

	/// Appends `for (i = begin; i < end; ++i) visit(body, i)` to `region`, or with `reverse`
	/// `for (i = end; i > begin; ) { --i; visit(body, i); }`. The index `i` is an int64 rvalue.
	void AppendIndexLoop(LoweringContext& ctx, BlockNode* region, const IndexRange& range, bool reverse,
		const std::function<void(BlockNode*, const HIRNode*)>& visit);
} // namespace Scripting
