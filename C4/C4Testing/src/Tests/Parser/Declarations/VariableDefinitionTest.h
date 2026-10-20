#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) VariableDefinitionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::VariableDefinitionTest);

		TEST_FUNC(Parser.VariableDefinition)
		void TestSimpleVariable();

		TEST_FUNC(Parser.VariableDefinition)
		void TestWithAccessModifier();

		TEST_FUNC(Parser.VariableDefinition)
		void TestStatic();

		TEST_FUNC(Parser.VariableDefinition)
		void TestConst();

		TEST_FUNC(Parser.VariableDefinition)
		void TestComplexType();

		TEST_FUNC(Parser.VariableDefinition)
		void TestWithAnnotation();

		TEST_FUNC(Parser.VariableDefinition)
		void TestNoMatch();
	};
}
