#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Backend/HIR/AsmTemplate and AsmRegisters (docs/language/16): the template in LLVM's terms, and the x86-64
	// register names an asm block may use.
	CLASS(GROUP = Testing) AsmLoweringTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AsmLoweringTest);

		TEST_FUNC(Semantic.Asm)
		void TemplateNamesAndPositions();
		TEST_FUNC(Semantic.Asm)
		void TemplateModifiersAndEscapes();
		TEST_FUNC(Semantic.Asm)
		void TemplateErrors();
		TEST_FUNC(Semantic.Asm)
		void RegisterKinds();
		TEST_FUNC(Semantic.Asm)
		void RegisterFamilies();
	};
} // namespace Testing
