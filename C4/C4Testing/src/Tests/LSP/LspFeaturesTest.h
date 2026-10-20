#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Server/CompilationSession.h"
#include "Features/Hover.h"
#include "Features/Definition.h"
#include "Features/References.h"
#include "Features/DocumentHighlight.h"
#include "Features/Completion.h"
#include "Features/DocumentSymbols.h"
#include "Features/WorkspaceSymbols.h"
#include "LspTestSupport.h"

namespace Testing
{
	// End-to-end language features against in-memory C4 sources: hover, go-to-definition,
	// completion and document symbols. Each test compiles a small program through the real
	// compiler frontend (Lexer -> Parser -> ModelBuilder) and exercises one feature.
	CLASS(GROUP = Testing) LspFeaturesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspFeaturesTest);

		// --- Hover ---
		TEST_FUNC(Lsp.Features.Hover)
		void TestHoverOnGlobalVariableShowsType();

		TEST_FUNC(Lsp.Features.Hover)
		void TestHoverOnFunctionCallCalleeShowsSignature();

		TEST_FUNC(Lsp.Features.Hover)
		void TestHoverOnClassFieldShowsFieldType();

		TEST_FUNC(Lsp.Features.Hover)
		void TestHoverOnLocalVariableShowsType();

		TEST_FUNC(Lsp.Features.Hover)
		void TestHoverWithNoSymbolReturnsNotFound();

		// --- Definition ---
		TEST_FUNC(Lsp.Features.Definition)
		void TestDefinitionOfGlobalVariableUseJumpsToDeclaration();

		TEST_FUNC(Lsp.Features.Definition)
		void TestDefinitionOfFunctionCallJumpsToDefinition();

		TEST_FUNC(Lsp.Features.Definition)
		void TestDefinitionOfClassFieldUseJumpsToField();

		TEST_FUNC(Lsp.Features.Definition)
		void TestDefinitionOnNonSymbolReturnsEmpty();

		// --- References ---
		TEST_FUNC(Lsp.Features.References)
		void TestReferencesFindsAllUsesOfGlobalVariable();

		TEST_FUNC(Lsp.Features.References)
		void TestReferencesOnFunctionCallFindsCalleeUse();

		TEST_FUNC(Lsp.Features.References)
		void TestReferencesOnNonSymbolReturnsEmpty();

		// --- Document highlight ---
		TEST_FUNC(Lsp.Features.DocumentHighlight)
		void TestDocumentHighlightMarksAllOccurrences();

		TEST_FUNC(Lsp.Features.DocumentHighlight)
		void TestDocumentHighlightKindsMatchVisualStudio();

		TEST_FUNC(Lsp.Features.DocumentHighlight)
		void TestDocumentHighlightOnNonSymbolReturnsEmpty();

	// --- Completion ---
	TEST_FUNC(Lsp.Features.Completion)
	void TestCompletionIncludesGlobalFunctionsAndTypes();

	TEST_FUNC(Lsp.Features.Completion)
	void TestCompletionFiltersByTypedPrefix();

	TEST_FUNC(Lsp.Features.Completion)
	void TestCompletionInsideFunctionIncludesParametersAndLocals();

	TEST_FUNC(Lsp.Features.Completion)
	void TestCompletionInsideClassIncludesMembers();

	TEST_FUNC(Lsp.Features.Completion)
	void TestCompletionMemberAccessDoesNotLeakSiblingFunctionLocal();


	// --- Document symbols ---
	TEST_FUNC(Lsp.Features.DocumentSymbols)
	void TestDocumentSymbolsListsTopLevelDeclarations();

	TEST_FUNC(Lsp.Features.DocumentSymbols)
	void TestDocumentSymbolsNestsClassMembers();

	// --- Workspace symbols (Ctrl+Shift+O picker) ---
	TEST_FUNC(Lsp.Features.WorkspaceSymbols)
	void TestWorkspaceSymbolsMatchesQueryCaseInsensitively();

	TEST_FUNC(Lsp.Features.WorkspaceSymbols)
	void TestWorkspaceSymbolsFindsMembersOfNonMatchingParent();

	TEST_FUNC(Lsp.Features.WorkspaceSymbols)
	void TestWorkspaceSymbolsEmptyQueryReturnsAllTopLevel();
	};
} // namespace Testing
