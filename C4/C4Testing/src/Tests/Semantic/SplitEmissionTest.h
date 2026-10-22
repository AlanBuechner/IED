#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A program emitted into partitions (CodeGenDriver::SplitInto, D-81, docs/compiler/05): each function and global
	// defined in one partition and declared where another names it, the locals other partitions name as hidden
	// globals, the program's own code in the first.
	CLASS(GROUP = Testing) SplitEmissionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SplitEmissionTest);

		TEST_FUNC(Semantic.SplitEmission)
		void EachFunctionIsDefinedInOnePartition();
		TEST_FUNC(Semantic.SplitEmission)
		void EachGlobalIsDefinedInOnePartition();
		TEST_FUNC(Semantic.SplitEmission)
		void LocalsOthersNameAreHiddenGlobals();
		TEST_FUNC(Semantic.SplitEmission)
		void TheProgramsOwnCodeIsInTheFirstPartition();
		TEST_FUNC(Semantic.SplitEmission)
		void OnePartitionKeepsLocalsLocal();
		TEST_FUNC(Semantic.SplitEmission)
		void ConstantsWithoutASymbolHaveNoStorage();
		TEST_FUNC(Semantic.SplitEmission)
		void AHostListsItsLocalsByTheirProgramNames();
	};
} // namespace Testing
