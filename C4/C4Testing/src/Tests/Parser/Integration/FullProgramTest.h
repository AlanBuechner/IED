#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Statements/IfStatementNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) FullProgramTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::FullProgramTest);

		TEST_FUNC(Parser.Integration)
		void TestFullProgram();

		TEST_FUNC(Parser.Integration)
		void TestEnumWithUnderlyingType();

		TEST_FUNC(Parser.Integration)
		void TestClassWithInheritance();

		TEST_FUNC(Parser.Integration)
		void TestTemplatedClass();

		TEST_FUNC(Parser.Integration)
		void TestFunctionWithBody();

		TEST_FUNC(Parser.Integration)
		void TestFunctionDeclaration();

		TEST_FUNC(Parser.Integration)
		void TestNestedControlFlow();

		TEST_FUNC(Parser.Integration)
		void TestCodeBlockDispatch();
	};
}
