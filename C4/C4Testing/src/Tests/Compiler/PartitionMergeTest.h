#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A whole-program build's cross-partition step (Compiler/PartitionMerge.h, R12): the partitions the program was
	// emitted into, read back into one module with its reflection image, every function and global defined once, the
	// locals local again under the names the program gave them.
	CLASS(GROUP = Testing) PartitionMergeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PartitionMergeTest);

		TEST_FUNC(Compiler.PartitionMerge)
		void EachDefinitionIsInTheMergedModuleOnce();
		TEST_FUNC(Compiler.PartitionMerge)
		void LocalsAreLocalAgainUnderTheirProgramNames();
		TEST_FUNC(Compiler.PartitionMerge)
		void TheReflectionImageJoinsTheProgram();
		TEST_FUNC(Compiler.PartitionMerge)
		void OnePartitionIsTheProgram();
		TEST_FUNC(Compiler.PartitionMerge)
		void AnUnreadablePartitionIsAnError();
	};
} // namespace Testing
