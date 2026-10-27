#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Include resolution and preprocessing with projects (docs/language/15-projects.md, rules 2 and 3).
	CLASS(GROUP = Testing) ProjectIncludeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ProjectIncludeTest);

		TEST_FUNC(SourceManager.Projects)
		void FilesKnowTheirProject();
		TEST_FUNC(SourceManager.Projects)
		void BesideTheIncluderComesFirst();
		TEST_FUNC(SourceManager.Projects)
		void OwnProjectBeforeDependencies();
		TEST_FUNC(SourceManager.Projects)
		void ADependencyOfADependencyIsReached();
		TEST_FUNC(SourceManager.Projects)
		void TwoDependenciesWithOnePathAreAmbiguous();
		TEST_FUNC(SourceManager.Projects)
		void TwoSourceFoldersWithOnePathAreAmbiguous();
		TEST_FUNC(SourceManager.Projects)
		void AProjectCannotReachItsUser();
		TEST_FUNC(SourceManager.Projects)
		void EachFileStartsWithItsProjectsMacros();
		TEST_FUNC(SourceManager.Projects)
		void AnIncludeCarriesOnlyWhatTheFileDefines();
	};
} // namespace Testing
