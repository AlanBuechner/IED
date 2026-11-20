#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A compile given the parses of the one before (CompilerInstance::UsePriorParses, Compiler/PriorParses): a file whose
	// text is the same takes its tree instead of being parsed, and asks for its includes again - so its include closure and
	// its errors are what a parse of it would give now. The language server's analysis after an edit (docs/tools/03).
	CLASS(GROUP = Testing) PriorParsesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PriorParsesTest);

		TEST_FUNC(Compiler.PriorParses)
		void AnUnchangedFileTakesItsTree();
		TEST_FUNC(Compiler.PriorParses)
		void ACompileWithTheTreesSaysWhatAFreshOneSays();
		TEST_FUNC(Compiler.PriorParses)
		void ATakenTreeReachesWhatItsIncludesIncludeNow();
		TEST_FUNC(Compiler.PriorParses)
		void ATakenTreeReportsItsIncludesAsAParseWould();
		TEST_FUNC(Compiler.PriorParses)
		void AFileNowThereAnswersATakenTreesInclude();
		TEST_FUNC(Compiler.PriorParses)
		void AnotherTextIsParsed();
	};
} // namespace Testing
