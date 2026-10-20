#pragma once
#include "Parser/Nodes/Expressions/LambdaNode.h"

namespace Scripting
{
	/// What a lambda's body refers to outside itself (D-22): the names that no parameter and no declaration
	/// inside the body introduces first, each once in first-use order, and whether it uses `this`. Whether a
	/// name is an enclosing local is the lowering's question; a name after `.`, `->` or `::`, a type and a
	/// label are never variables.
	struct OuterUses
	{
		Core::Vector<Core::String> names;
		bool usesThis = false;
	};

	OuterUses FindOuterUses(const LambdaNode& lambda);
}
