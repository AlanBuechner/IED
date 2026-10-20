#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) ParameterTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ParameterTest);

		TEST_FUNC(Parser.Foundation.Parameter.Basic)
		void TestBasicParameter();

		TEST_FUNC(Parser.Foundation.Parameter.DefaultValue)
		void TestParameterWithDefault();

		TEST_FUNC(Parser.Foundation.Parameter.Unnamed)
		void TestUnnamedParameter();

		TEST_FUNC(Parser.Foundation.Parameter.Annotation)
		void TestAnnotatedParameter();

		TEST_FUNC(Parser.Foundation.Parameter.Complex)
		void TestComplexTypeWithDefault();

		TEST_FUNC(Parser.Foundation.Parameter.MultipleAnnotations)
		void TestMultipleAnnotations();

		TEST_FUNC(Parser.Foundation.Parameter.Negative)
		void TestNonTypeTokenReturnsNullptr();
	};
}
