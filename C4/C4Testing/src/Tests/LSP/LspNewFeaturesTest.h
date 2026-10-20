#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Server/CompilationSession.h"
#include "Features/SignatureHelp.h"
#include "Features/TypeDefinition.h"
#include "Features/Implementation.h"
#include "Features/Rename.h"
#include "Features/Definition.h"
#include "LspTestSupport.h"

namespace Testing
{
	// Language features added on top of the core navigation set: signature help, go-to-type-
	// definition, go-to-implementation (derived types), and cross-file rename. Each test compiles
	// a small C4 program through the real frontend and exercises one feature.
	CLASS(GROUP = Testing) LspNewFeaturesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspNewFeaturesTest);

		// --- Signature help ---
		TEST_FUNC(Lsp.Features.SignatureHelp)
		void TestSignatureHelpShowsCalleeParameters();

		TEST_FUNC(Lsp.Features.SignatureHelp)
		void TestSignatureHelpTracksActiveArgument();

		TEST_FUNC(Lsp.Features.SignatureHelp)
		void TestSignatureHelpOnNonCallReturnsNotFound();

		// --- Type definition ---
		TEST_FUNC(Lsp.Features.TypeDefinition)
		void TestTypeDefinitionFromVariableDeclarationJumpsToStruct();

		TEST_FUNC(Lsp.Features.TypeDefinition)
		void TestTypeDefinitionOnNonSymbolReturnsEmpty();

		// --- Implementation (derived types) ---
		TEST_FUNC(Lsp.Features.Implementation)
		void TestImplementationFindsDerivedTypes();

		TEST_FUNC(Lsp.Features.Implementation)
		void TestImplementationOfBaseWithNoDerivativesIsEmpty();

		// --- Include navigation (go-to-definition on `include "x.ch"`) ---
		TEST_FUNC(Lsp.Features.IncludeNavigation)
		void TestIncludeDefinitionNavigatesToIncludedFile();

		TEST_FUNC(Lsp.Features.IncludeNavigation)
		void TestIncludeDefinitionUnresolvablePathReturnsEmpty();

		TEST_FUNC(Lsp.Features.IncludeNavigation)
		void TestSymbolDefinitionStillWorksBesideIncludes();

		// --- Rename ---
		TEST_FUNC(Lsp.Features.Rename)
		void TestRenameGlobalVariableCoversDeclarationAndUses();

		TEST_FUNC(Lsp.Features.Rename)
		void TestRenameFunctionCoversDefinitionAndCalls();

		TEST_FUNC(Lsp.Features.Rename)
		void TestRenameOnNonSymbolReturnsNotFound();

		TEST_FUNC(Lsp.Features.Rename)
		void TestPrepareRenameSelectsNameTokenAtReferenceAndDeclaration();

		TEST_FUNC(Lsp.Features.Rename)
		void TestPrepareRenameOnNonSymbolReturnsNotFound();
	};
} // namespace Testing
