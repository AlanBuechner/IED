#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The object cache and the module split under it (Compiler/ObjectCache.h, Compiler/ModulePartitions.h; roadmap 5.3).
	CLASS(GROUP = Testing) ObjectCacheTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ObjectCacheTest);

		TEST_FUNC(Compiler.ObjectCache)
		void TheSameCodeNamesTheSameObject();
		TEST_FUNC(Compiler.ObjectCache)
		void ChangedCodeNamesAnotherObject();
		TEST_FUNC(Compiler.ObjectCache)
		void AnotherCompilerOrTargetNamesAnotherObject();
		TEST_FUNC(Compiler.ObjectCache)
		void KeepOnlyRemovesEverythingElse();

		TEST_FUNC(Compiler.ModulePartitions)
		void EveryPartitionReadsBackAndVerifies();
		TEST_FUNC(Compiler.ModulePartitions)
		void EachDefinitionIsInOnePartition();
		TEST_FUNC(Compiler.ModulePartitions)
		void PrivateDataLivesWithItsUser();
		TEST_FUNC(Compiler.ModulePartitions)
		void LocalsBecomeSuffixedHiddenGlobals();
		TEST_FUNC(Compiler.ModulePartitions)
		void OnlyTheFirstPartitionKeepsFileScopeAssembly();
		TEST_FUNC(Compiler.ModulePartitions)
		void AddingAFunctionLeavesTheOtherPartitionsUnchanged();
	};
} // namespace Testing
