#pragma once
#include "Backend/HIR/HIRNodes.h"
#include <vector>

namespace Scripting
{
	/// The census's counter (docs/runtime/01) code that allocates objects of `type` needs: of one object, or with
	/// `countBytes`, of a counted array of them (CountedArray). Only for a type that can hold native memory.
	struct CensusNeed
	{
		const Type* type = nullptr;
		uint64 countBytes = 0;

		bool operator==(const CensusNeed&) const = default;
	};

	/// What a lowered body's code names of the rest of the program, as codegen emits it (HIRWalk): the functions it calls,
	/// takes the address of, constructs objects with, registers as finalizers or exit destroyers, and copies and destroys
	/// exceptions with; the types of the objects whose vptrs it stores (their vtables' entries, VtableEntries); the census
	/// counters of the objects it allocates. In the order the tree has them, then its slots', then its nested functions'.
	struct BodyNeeds
	{
		std::vector<const FunctionRecord*> functions;
		std::vector<const Type*> objects;
		std::vector<CensusNeed> census;
	};

	BodyNeeds NeedsOf(const HIRFunction& body);
} // namespace Scripting
