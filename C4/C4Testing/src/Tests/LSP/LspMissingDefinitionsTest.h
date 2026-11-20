#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Visual Studio's "Function definition for 'X' not found" (MissingDefinitions) and its fixes: the definition created in
	// the source file beside the neighbours' definitions (as Visual Assist's Create Implementation places it), in place of the
	// declaration's `;`, or all at once (DefinitionSites, DefinitionWriter). Each fix is applied and the program analyzed
	// again: it compiles and the warning is gone.
	CLASS(GROUP = Testing) LspMissingDefinitionsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspMissingDefinitionsTest);

		// --- The warning ---

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestAMethodWithoutABodyIsReported();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestAFunctionDefinedInAnyFileIsNot();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestPureAndForeignFunctionsNeedNoBody();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestOnlyTheOverloadWithoutABodyIsReported();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestTemplatesAreReportedWithAndWithoutUses();

		// Claymore's Entity::GetComponent<T>: the record of a member template has no body, whatever is written for it.
		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestAMemberTemplateDefinedInAnotherFileIsNot();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestTurnedOffReportsNothing();

		// --- Where the definition goes ---

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestTheDefinitionFollowsThePreviousNeighbour();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestTheDefinitionPrecedesTheNextNeighbourAndItsComment();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestWithoutNeighboursItEndsTheNamespaceBlock();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestAtTheFileEndItOpensItsNamespaces();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestTheDefinitionIsWrittenAsTheFileWritesThem();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestWithoutASourceFileOnlyInPlaceIsOffered();

		// --- What it says ---

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestTheSignatureDropsDefaultsAndKeepsQualifiers();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestEveryFixCompilesAndClearsTheWarning();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestAReferenceReturnGetsAnEmptyBody();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestInPlaceReplacesTheSemicolon();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestATemplatesDefinitionKeepsItsHeader();

		TEST_FUNC(Lsp.Features.MissingDefinitions)
		void TestSnippetEditsPlaceTheCursor();
	};
} // namespace Testing
