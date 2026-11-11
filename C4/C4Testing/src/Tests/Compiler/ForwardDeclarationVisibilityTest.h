#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A type forward-declared in one header and defined in another is visible to every file that includes either, in
	// whichever order the compiler meets them (BUG-439: the definition's own file could not name it).
	CLASS(GROUP = Testing) ForwardDeclarationVisibilityTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ForwardDeclarationVisibilityTest);

		TEST_FUNC(Compiler.ForwardDeclarationVisibility)
		void TheDefinitionsFileNamesItWhenTheForwardDeclarationComesFirst();
		TEST_FUNC(Compiler.ForwardDeclarationVisibility)
		void AFileIncludingOnlyTheForwardDeclarationNamesIt();
		TEST_FUNC(Compiler.ForwardDeclarationVisibility)
		void AFileIncludingNeitherCannotNameIt();
	};
} // namespace Testing
