#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Model/OperatorTable and the model's operator lookup (D-23..D-26): a token binds by the method's arity,
	// every name of an annotation registers, and the left operand alone is the receiver.
	CLASS(GROUP = Testing) OperatorTableTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::OperatorTableTest);

		TEST_FUNC(Semantic.OperatorTable)
		void ArityPicksTheMeaning();
		TEST_FUNC(Semantic.OperatorTable)
		void CompletedNames();
		TEST_FUNC(Semantic.OperatorTable)
		void EveryNameRegisters();
		TEST_FUNC(Semantic.OperatorTable)
		void PrimitiveExtensionsAreReceivers();
	};
} // namespace Testing
