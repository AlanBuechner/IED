#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// BUG-303: an out-of-line method definition (`int32 Counter::Next()`) is named by its last segment, never by
	// the type that qualifies it, in every feature that names or edits a function, and it is the same symbol as
	// its declaration. BUG-304: references are found in every file.
	CLASS(GROUP = Testing) LspQualifiedDefinitionsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspQualifiedDefinitionsTest);

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestWorkspaceSymbolsNameTheMethod();

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestDocumentSymbolsNameTheMethod();

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestHoverNamesTheMethod();

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestDefinitionLandsOnTheBody();

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestRenameLeavesTheQualifyingType();

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestHighlightMarksBothDeclarations();

		TEST_FUNC(Lsp.Features.QualifiedDefinitions)
		void TestReferencesSearchEveryFile();
	};
} // namespace Testing
