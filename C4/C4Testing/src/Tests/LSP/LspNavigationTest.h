#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Server/CompilationSession.h"
#include "Features/Hover.h"
#include "Features/DocumentSymbols.h"
#include "Features/WorkspaceSymbols.h"
#include "Features/CallHierarchy.h"
#include "Features/TypeHierarchy.h"
#include "Features/MacroUses.h"
#include "Features/DeclarationText.h"
#include "Features/References.h"
#include "Features/DocumentHighlight.h"
#include "Features/TypeDefinition.h"
#include "Features/Implementation.h"
#include "LspTestSupport.h"

namespace Testing
{
	// What code navigation tools read about C4 code (docs/17 L10-L14): symbols that say what a member is, hover with
	// the declaration as written and its doc comment, call and type hierarchies, and macro uses expanded.
	CLASS(GROUP = Testing) LspNavigationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspNavigationTest);

		// --- L10: symbols ---
		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestSymbolKindsSayWhatAMemberIs();

		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestSymbolDetailIsTheDeclarationWithoutItsName();

		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestOutOfLineDefinitionsAreNamedWithTheirOwner();

		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestWorkspaceSymbolContainersAreQualifiedNames();

		// --- L11: hover ---
		TEST_FUNC(Lsp.Navigation.Hover)
		void TestHoverShowsTheDeclarationAsWrittenWithItsOwnerAndComment();

		TEST_FUNC(Lsp.Navigation.Hover)
		void TestHoverOnAFieldUsesTheCommentAfterIt();

		TEST_FUNC(Lsp.Navigation.Hover)
		void TestHoverOnAMacroUseShowsItsDefinitionAndExpansion();

		// --- L12: hierarchies ---
		TEST_FUNC(Lsp.Navigation.CallHierarchy)
		void TestCallHierarchyFindsCallersAndCallees();

		TEST_FUNC(Lsp.Navigation.CallHierarchy)
		void TestCallHierarchyOffAFunctionIsEmpty();

		TEST_FUNC(Lsp.Navigation.TypeHierarchy)
		void TestTypeHierarchyFindsSupertypesAndSubtypes();

		// --- L13: macro expansion ---
		TEST_FUNC(Lsp.Navigation.Macros)
		void TestExpandMacroExpandsTheUseAtAPosition();

		// --- L14: ranges and macros as symbols ---
		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestSymbolRangesCoverDocCommentsAndAnnotations();

		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestMacrosAreDocumentSymbols();

		// --- Found by IED's code tools ---
		// BUG-349: references and highlights from a declaration's own name, not only from a use.
		TEST_FUNC(Lsp.Navigation.References)
		void TestReferencesFromADeclarationsOwnName();

		// A type's uses where it is written as a type (a parameter's, a local's) are its references.
		TEST_FUNC(Lsp.Navigation.References)
		void TestReferencesOfATypeIncludeWhereItIsWritten();

		// BUG-350: the type definition of an object-typed variable, at its declaration and at a use (it never answered).
		TEST_FUNC(Lsp.Navigation.TypeDefinition)
		void TestTypeDefinitionOfAVariable();

		TEST_FUNC(Lsp.Navigation.Implementation)
		void TestImplementationOfAMethodIsWhatOverridesIt();

		TEST_FUNC(Lsp.Navigation.Symbols)
		void TestWorkspaceSymbolsIncludeMacros();

		// BUG-353: a member reached with `->` through a pointer (`server->documents->ApplyEdit(...)`) resolves.
		TEST_FUNC(Lsp.Navigation.References)
		void TestMembersReachedThroughPointersResolve();
	};
}
