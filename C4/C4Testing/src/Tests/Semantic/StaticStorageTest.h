#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Objects with static storage (docs/language/06, BUG-105): a static local starts in a once region and a
	// constant one needs none; globals and static members start in `__c4_global_init`, in declaration order;
	// each that needs destruction is registered for exit.
	CLASS(GROUP = Testing) StaticStorageTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StaticStorageTest);

		TEST_FUNC(Semantic.StaticStorage)
		void ConstantStaticLocalNeedsNoGuard();

		TEST_FUNC(Semantic.StaticStorage)
		void ObjectStaticLocalStartsOnceAndEndsAtExit();

		TEST_FUNC(Semantic.StaticStorage)
		void AbandonedStartZeroesTheStorage();

		TEST_FUNC(Semantic.StaticStorage)
		void GlobalsStartInDeclarationOrder();

		TEST_FUNC(Semantic.StaticStorage)
		void ConstantGlobalsNeedNoStartup();
		TEST_FUNC(Semantic.StaticStorage)
		void FoldedConstantsAreReadAsValues();

		TEST_FUNC(Semantic.StaticStorage)
		void FailedGlobalInitializerIsAnError();

		TEST_FUNC(Semantic.StaticStorage)
		void InstanceStaticsStartInLaterRounds();

		TEST_FUNC(Semantic.StaticStorage)
		void ThreadLocalsStartInThreadRounds();

		TEST_FUNC(Semantic.StaticStorage)
		void ThreadLocalLocalStartsOnEachThread();
	};
} // namespace Testing
