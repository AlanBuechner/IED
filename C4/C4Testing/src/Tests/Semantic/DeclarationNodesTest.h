#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Every model entity keeps the declaration it came from (BUG-154): the LSP navigates to it, diagnostics
	// point at it, and reflection reads its annotations.
	CLASS(GROUP = Testing) DeclarationNodesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DeclarationNodesTest);

		TEST_FUNC(Semantic.DeclarationNodes)
		void ObjectTypeKeepsItsDeclaration();

		TEST_FUNC(Semantic.DeclarationNodes)
		void EnumKeepsItsDeclarationAndScope();

		TEST_FUNC(Semantic.DeclarationNodes)
		void TemplateInstanceKeepsTheTemplatesDeclaration();

		TEST_FUNC(Semantic.DeclarationNodes)
		void FunctionKeepsEveryDeclaration();

		TEST_FUNC(Semantic.DeclarationNodes)
		void MethodKeepsInClassAndOutOfLineDeclarations();
	};
}
