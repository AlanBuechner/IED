#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What the debug adapter's `evaluate` reads: a variable, scoped names, field and index chains, leading
	// dereferences, and the errors for everything else.
	CLASS(GROUP = Testing) DapExpressionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapExpressionTest);

		TEST_FUNC(Dap.Expression)
		void AVariableAlone();
		TEST_FUNC(Dap.Expression)
		void ScopedNamesStayWhole();
		TEST_FUNC(Dap.Expression)
		void FieldsPointersAndIndexesChain();
		TEST_FUNC(Dap.Expression)
		void LeadingStarsDereference();
		TEST_FUNC(Dap.Expression)
		void MalformedTextIsAnError();
	};
} // namespace Testing
