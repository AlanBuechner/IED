#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a script's project depends on and which of its host's files a compile reads (Script/ScriptReach, docs/runtime/04
	// section 4, step 3): the projects its allow-list names, else everything below it; the files of what it reaches, the
	// library's, those adding to a template it reads and every unit of a collided symbol it reads one of; and layers'.
	CLASS(GROUP = Testing) ScriptReachTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScriptReachTest);

		TEST_FUNC(Script.Reach)
		void AnAllowListOfProjectsIsWhatItDependsOn();
		TEST_FUNC(Script.Reach)
		void APathOrNoListDependsOnEverythingBelow();
		TEST_FUNC(Script.Reach)
		void NothingAllowedDependsOnNothing();
		TEST_FUNC(Script.Reach)
		void ACompileReadsWhatItsProjectReachesAndTheLibrary();
		TEST_FUNC(Script.Reach)
		void AFileAddingToATemplateItReadsIsRead();
		TEST_FUNC(Script.Reach)
		void AnAdditionToAnotherNamespacesTemplateIsNot();
		TEST_FUNC(Script.Reach)
		void EveryUnitOfACollidedSymbolIsRead();
		TEST_FUNC(Script.Reach)
		void ALayerReachesWhatItDependsOn();
	};
} // namespace Testing
