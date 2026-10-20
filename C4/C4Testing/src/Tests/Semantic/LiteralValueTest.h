#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Literal values and types (Model/Literals, B2): C's first-type-that-fits rule per suffix and radix, the
	// limits, and character values. Lowering and constant folding both read literals here (BUG-006, BUG-121).
	CLASS(GROUP = Testing) LiteralValueTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LiteralValueTest);

		TEST_FUNC(Semantic.Literals.Integers)
		void IntegersTakeTheFirstTypeThatFits();
		TEST_FUNC(Semantic.Literals.Integers)
		void IntegerValuesAndLimits();
		TEST_FUNC(Semantic.Literals.Floats)
		void FloatTypesAndRange();
		TEST_FUNC(Semantic.Literals.Characters)
		void CharacterValues();
	};
} // namespace Testing
