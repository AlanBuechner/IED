#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R7: what parsed in a file with syntax errors is in the model, so the editor answers there as in a file without any.
	CLASS(GROUP = Testing) LspRecoveryTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspRecoveryTest);

		TEST_FUNC(LSP.Recovery)
		void HoverAfterABrokenDeclaration();

		TEST_FUNC(LSP.Recovery)
		void DefinitionAfterABrokenDeclaration();

		TEST_FUNC(LSP.Recovery)
		void CompletionAfterABrokenDeclaration();

		TEST_FUNC(LSP.Recovery)
		void MembersOfATypeWithABrokenMember();

		TEST_FUNC(LSP.Recovery)
		void DeclarationsAfterABraceLeftOpen();

		TEST_FUNC(LSP.Recovery)
		void EachMistakeIsOneDiagnostic();
	};
}
