#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Who owns the module a compile builds (BUG-240): the driver, until the caller takes it.
	CLASS(GROUP = Testing) ModuleOwnershipTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ModuleOwnershipTest);

		TEST_FUNC(Semantic.ModuleOwnership)
		void TheModuleGoesWithItsDriver();
		TEST_FUNC(Semantic.ModuleOwnership)
		void ATakenModuleOutlivesItsDriver();
	};
} // namespace Testing
