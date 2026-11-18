#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A type as an annotation's argument (`[Requires(Render::MeshRenderer)]`, D-42) is code the editor knows: its definition,
	// hover, references, completion after `Render::`, its color, and an error where a name is neither a constant nor a type.
	CLASS(GROUP = Testing) LspAnnotationArgumentsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspAnnotationArgumentsTest);

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestDefinitionOfAQualifiedType();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestDefinitionOfANameFromTheNamespace();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestDefinitionOfATemplateInstance();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestDefinitionOfAConstant();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestHoverNamesTheType();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestReferencesFindTheAnnotation();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestCompletionAfterANamespace();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestCompletionAfterANamespaceWithAPrefix();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestCompletionAfterATypeListsItsMembers();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestAnUnknownNameIsADiagnostic();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestADeclaredAnnotationsNameLeadsToItsDeclaration();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestADeclaredAnnotationsUsesAreItsReferences();

		TEST_FUNC(Lsp.Features.AnnotationArguments)
		void TestAMisspeltAnnotationIsADiagnostic();
	};
} // namespace Testing
