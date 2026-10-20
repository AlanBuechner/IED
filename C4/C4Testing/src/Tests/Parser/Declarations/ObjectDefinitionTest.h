#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) ObjectDefinitionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ObjectDefinitionTest);

		TEST_FUNC(Parser.ObjectDefinition)
		void TestSimpleClass();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestStruct();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestInterface();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestWithAccessModifier();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestWithInheritance();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestMultipleInheritance();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestWithTemplate();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestWithAnnotation();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestAbstractClass();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestFinalClass();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestClassModifierConflicts();

		TEST_FUNC(Parser.ObjectDefinition)
		void TestNoMatch();
	};
}
