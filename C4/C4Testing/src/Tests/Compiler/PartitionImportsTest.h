#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Partitions optimized alone (an incremental build, D-76; Compiler/PartitionImports.h, Compiler/ModulePartitions.h):
	// copies of small callees, definitions kept for other partitions, and bitcode that follows what was copied.
	CLASS(GROUP = Testing) PartitionImportsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PartitionImportsTest);

		TEST_FUNC(Compiler.PartitionImports)
		void SmallCalleesAreCopiedAvailableExternally();
		TEST_FUNC(Compiler.PartitionImports)
		void LargeAndNoInlineCalleesAreOnlyDeclared();
		TEST_FUNC(Compiler.PartitionImports)
		void ImportsReachThroughSmallCallees();
		TEST_FUNC(Compiler.PartitionImports)
		void MinSizeCallersCopyNothing();
		TEST_FUNC(Compiler.PartitionImports)
		void LinkOnceDefinitionsAreKept();
		TEST_FUNC(Compiler.PartitionImports)
		void AnImportedBodyChangesItsCallersPartitions();
		TEST_FUNC(Compiler.PartitionImports)
		void EveryPartitionVerifies();
	};
} // namespace Testing
