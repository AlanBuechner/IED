#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The lowering-time scope layers of the symbol table: a pushed layer is the current one until its guard ends,
	// then its parent is again, and the layer and its names leave the tree with it.
	CLASS(GROUP = Testing) ScopeGuardTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScopeGuardTest);

		TEST_FUNC(Semantic.ScopeGuard)
		void APushedLayerIsCurrentUntilItsGuardEnds();
		TEST_FUNC(Semantic.ScopeGuard)
		void NestedGuardsUnwindInOrder();
		TEST_FUNC(Semantic.ScopeGuard)
		void ANameInALayerLeavesWithIt();
		TEST_FUNC(Semantic.ScopeGuard)
		void ALayerSeesItsParentsNames();
	};
} // namespace Testing
