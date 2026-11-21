#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// One visibility rule (D-88): a declaration, like a body, sees the names its file includes, a template's instance
	// sees what the template's file includes, and a name declared in a file the asking one does not include is an
	// error naming that file.
	CLASS(GROUP = Testing) IncludeVisibilityTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::IncludeVisibilityTest);

		TEST_FUNC(Semantic.IncludeVisibility)
		void ADeclarationNamesWhatItsFileIncludes();
		TEST_FUNC(Semantic.IncludeVisibility)
		void EveryKindOfDeclarationNeedsTheInclude();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnIncludeReachesWhatItIncludes();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AForwardDeclarationIsEnoughForAPointer();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AConstantInADeclarationNeedsItsHeader();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AClassTemplateNeedsItsHeader();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnInstanceSeesWhatItsTemplatesFileIncludes();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnInstanceDoesNotSeeWhatOnlyItsUserIncludes();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnInstancesConstantNeedsItsTemplatesInclude();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnInstanceNamesItsTemplatesNamespace();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnInstanceIsTheSameWhoeverAsksFirst();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnOutOfLineTemplateMethodSeesItsOwnFile();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AFunctionTemplateIsFoundAsAFunctionIs();
		TEST_FUNC(Semantic.IncludeVisibility)
		void AnUnseenOverloadIsNamedBesideAMismatch();
		TEST_FUNC(Semantic.IncludeVisibility)
		void ATemplateDeclaredInAHeaderIsDefinedInItsSource();
		TEST_FUNC(Semantic.IncludeVisibility)
		void MembersAreReachedThroughTheirObject();
		TEST_FUNC(Semantic.IncludeVisibility)
		void ToolsAskingForNoFileSeeEverything();
	};
} // namespace Testing
