#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) FunctionDefinitionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::FunctionDefinitionTest);

		TEST_FUNC(Parser.FunctionDefinition)
		void TestSimpleDeclaration();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestWithBody();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestWithAccessModifier();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestStaticFunction();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestVirtualFunction();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestWithParameters();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestWithTemplate();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestWithAnnotation();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestConstQualifier();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestNoexceptQualifier();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestQualifiersCombine();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestPureVirtual();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestFinalEitherSide();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestAbstractIsPureVirtual();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestRejectedModifiers();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestVariadic();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestComplexQualifiedName();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestNoMatch();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestParameterPack();

		TEST_FUNC(Parser.FunctionDefinition)
		void TestOwnerTemplateHeader();
	};
}
