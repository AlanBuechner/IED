#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R8: the editor shows a declaration as its parse read it - a macro use as its expansion - while every position stays
	// the file's own.
	CLASS(GROUP = Testing) LspDeclarationTextTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspDeclarationTextTest);

		TEST_FUNC(LSP.DeclarationText)
		void AMacroUseInADeclarationReadsAsItsExpansion();

		TEST_FUNC(LSP.DeclarationText)
		void ADeclarationAfterAMacroUseOnItsLineIsFound();
	};
}
