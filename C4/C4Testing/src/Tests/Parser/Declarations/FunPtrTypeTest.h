#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/FunPtrTypeNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) FunPtrTypeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::FunPtrTypeTest);

		TEST_FUNC(Parser.FunPtrType)
		void TestSimpleFunPtrType();

		TEST_FUNC(Parser.FunPtrType)
		void TestNestedFunPtrType();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrTypeInQualType();

		TEST_FUNC(Parser.FunPtrType)
		void TestNestedFunPtrInQualType();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrVariableDefinition();

		TEST_FUNC(Parser.FunPtrType)
		void TestNestedFunPtrVariableDefinition();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrFunctionParameter();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrReturnFunction();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrInNamespaceBlock();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrFunctionInNamespaceBlock();

		TEST_FUNC(Parser.FunPtrType)
		void TestNestedFunPtrInNamespaceBlock();

		TEST_FUNC(Parser.FunPtrType)
		void TestFunPtrPointerVariable();

		TEST_FUNC(Parser.FunPtrType)
		void TestAliasFunPtrType();

		TEST_FUNC(Parser.FunPtrType)
		void TestVoidParameterList();
	};
}
