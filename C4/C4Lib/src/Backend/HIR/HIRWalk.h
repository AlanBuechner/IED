#pragma once
#include "Backend/HIR/HIRNodes.h"
#include <functional>

namespace C4Compiler
{
	/// What a walk of a lowered body is told: each node and each lvalue codegen emits.
	struct HIRVisitor
	{
		std::function<void(const HIRNode&)> node;
		std::function<void(const LValue&)> lvalue;
	};

	/// Visits what codegen emits of `function`'s own tree: every node and lvalue reachable from its root, a node before
	/// its operands, statements in order. A node the lowering made and left out of the tree (a copy a last use moved
	/// instead, a speculative lowering) is not visited; nor are the nested functions, each a tree of its own.
	void WalkEmitted(const HIRFunction& function, const HIRVisitor& visit);
} // namespace C4Compiler
