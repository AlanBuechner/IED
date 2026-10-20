#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) NamespaceQualifiedNameTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::NamespaceQualifiedNameTest);

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.Basic)
		void TestSingleIdentifier();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.Basic)
		void TestTwoSegments();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.TemplateArgs)
		void TestWithTemplateArgsOnOneSegment();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.TemplateArgs)
		void TestNestedTemplateArgs();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.Complex)
		void TestComplexChain();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.QualTypeArgs)
		void TestTemplateArgWithConstPointer();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.QualTypeArgs)
		void TestTemplateArgWithNestedTypePointerRef();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.QualTypeArgs)
		void TestMultipleTemplateArgsWithQualTypes();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.QualTypeArgs)
		void TestTemplateArgWithQualTypeAndNestedTemplates();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.Negative)
		void TestNonIdentifierStartsReturnsNullptr();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.ErrorRecovery)
		void TestMissingIdentifierAfterScopeResolution();

		TEST_FUNC(Parser.Foundation.NamespaceQualifiedName.ErrorRecovery)
		void TestMissingCloseAngleInTemplateArgs();
	};
}
