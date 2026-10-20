#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A file's interface text (Script/InterfaceText, docs/runtime/04 section 3.2): non-template bodies go, templates
	// and declarations stay, lines keep their numbers, and the result parses.
	CLASS(GROUP = Testing) InterfaceTextTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::InterfaceTextTest);

		TEST_FUNC(Script.InterfaceText)
		void FunctionBodyBecomesADeclaration();
		TEST_FUNC(Script.InterfaceText)
		void MethodBodiesBecomeDeclarations();
		TEST_FUNC(Script.InterfaceText)
		void OutOfLineDefinitionDisappears();
		TEST_FUNC(Script.InterfaceText)
		void AnnotationsGoWithAnOutOfLineDefinition();
		TEST_FUNC(Script.InterfaceText)
		void ADefinitionDeclaredElsewhereDisappears();
		TEST_FUNC(Script.InterfaceText)
		void ALeadingConstGoesWithItsDefinition();
		TEST_FUNC(Script.InterfaceText)
		void TemplatesKeepTheirBodies();
		TEST_FUNC(Script.InterfaceText)
		void NamespacesAndExtensionsAreWalked();
		TEST_FUNC(Script.InterfaceText)
		void InitializersStay();
		TEST_FUNC(Script.InterfaceText)
		void LinesKeepTheirNumbers();
		TEST_FUNC(Script.InterfaceText)
		void TheResultParses();
		TEST_FUNC(Script.InterfaceText)
		void NodesOfAnotherTextFail();
	};
} // namespace Testing
