#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) TemplateDefinitionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TemplateDefinitionTest);

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Basic)
		void TestSingleTypeParam();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Basic)
		void TestMultipleParams();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.NonType)
		void TestNonTypeParam();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Mixed)
		void TestMixedParams();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Keyword)
		void TestTypenameKeyword();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Keyword)
		void TestClassKeyword();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Variadic)
		void TestVariadicType();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Variadic)
		void TestVariadicNonType();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.Negative)
		void TestNoTemplateAtStart();

		TEST_FUNC(Parser.Foundation.TemplateDefinition.ErrorRecovery)
		void TestMissingCloseAngle();
	};
}
