#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A build's projects (D-59, docs/language/15-projects.md): reading the description and checking the set.
	CLASS(GROUP = Testing) ProjectsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ProjectsTest);

		TEST_FUNC(Compiler.Projects)
		void ReadsADescription();
		TEST_FUNC(Compiler.Projects)
		void AnErrorNamesItsLine();
		TEST_FUNC(Compiler.Projects)
		void NamesAndDependenciesAreChecked();
		TEST_FUNC(Compiler.Projects)
		void ACycleIsPrinted();
		TEST_FUNC(Compiler.Projects)
		void AFolderBelongsToOneProject();
		TEST_FUNC(Compiler.Projects)
		void EveryProjectServesTheRoot();
		TEST_FUNC(Compiler.Projects)
		void AProjectUsesWhatItDependsOn();
		TEST_FUNC(Compiler.Projects)
		void FilesBelongToTheirProject();
		TEST_FUNC(Compiler.Projects)
		void ADeepDescriptionReads();
	};
} // namespace Testing
