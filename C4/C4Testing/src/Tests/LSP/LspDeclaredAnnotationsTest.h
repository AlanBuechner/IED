#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A declared annotation in the editor (docs/language/12 section 4.4): go-to-definition and hover on its name, its uses as the
	// references and rename of its declarations, completion of its name inside `[...]` by where it goes and of the places after
	// `on`, signature help with every overload inside its parentheses, its declaration highlighted and in the outline, and its
	// errors as diagnostics.
	CLASS(GROUP = Testing) LspDeclaredAnnotationsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspDeclaredAnnotationsTest);

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestANameLeadsToTheOverloadItsArgumentsFit();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestHoverShowsEveryOverload();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestHoverOnADeclarationShowsTheAnnotation();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestCompletionOfPlacesAfterOn();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestCompletionFollowsNamespaces();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestTheDeclarationIsHighlightedAndListed();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestItsUsesAreTheReferencesOfEveryOverload();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestRenameRenamesEveryDeclarationAndUse();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestCompletionOffersWhatGoesThere();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestCompletionOnAnAccessLabel();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestCompletionAfterAComma();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestNoAnnotationsInAnIndex();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestSignatureHelpShowsEveryOverload();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestAMisspeltAnnotationIsADiagnostic();

		TEST_FUNC(Lsp.Features.DeclaredAnnotations)
		void TestAWrongArgumentIsADiagnosticWhereItIs();
	};
} // namespace Testing
