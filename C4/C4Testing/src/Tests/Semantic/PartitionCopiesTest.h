#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a partition of a program emitted in partitions copies from the others for its optimizer to inline
	// (ProgramBackend::SplitInto, Backend/CodeGen/ImportEmitter, ImportPlanner; D-76, R12): the small functions it calls
	// and what they call in turn when its partitions are optimized alone, only `inline` ones at -O0, nothing when they are
	// merged into one module first (a whole-program build).
	CLASS(GROUP = Testing) PartitionCopiesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PartitionCopiesTest);

		TEST_FUNC(Semantic.PartitionCopies)
		void SmallCalleesAreCopiedAvailableExternally();
		TEST_FUNC(Semantic.PartitionCopies)
		void LargeCalleesAreOnlyDeclared();
		TEST_FUNC(Semantic.PartitionCopies)
		void CopiesReachThroughSmallCallees();
		TEST_FUNC(Semantic.PartitionCopies)
		void AtO0OnlyInlineFunctionsAreCopied();
		TEST_FUNC(Semantic.PartitionCopies)
		void PartitionsMergedFirstCopyNothing();
		TEST_FUNC(Semantic.PartitionCopies)
		void EveryPartitionVerifies();
	};
} // namespace Testing
