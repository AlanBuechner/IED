#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// BODY NEEDS (Backend/Lowering/BodyNeeds, Backend/HIR/HIRWalk, Backend/Lowering/VtableEntries): what a lowered body's
	// emitted code names of the rest of the program - followed through the tree codegen emits, never through nodes the
	// lowering made and left out - and the functions an object's vtables point at.
	CLASS(GROUP = Testing) BodyNeedsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BodyNeedsTest);

		TEST_FUNC(Lowering.BodyNeeds)
		void CallsConstructionsAndAddressesAreNeeded();

		TEST_FUNC(Lowering.BodyNeeds)
		void AMovedCopyIsNotNeeded();

		TEST_FUNC(Lowering.BodyNeeds)
		void AThrowNeedsItsDestroyAndCopy();

		TEST_FUNC(Lowering.BodyNeeds)
		void AnAllocationNeedsItsCensusCounterAndFinalizer();

		TEST_FUNC(Lowering.BodyNeeds)
		void ALocalsObjectIsHeld();

		TEST_FUNC(Lowering.VtableEntries)
		void AnObjectsVtablesHoldItsFinalOverriders();

		TEST_FUNC(Lowering.VtableEntries)
		void FieldsAndElementsBringTheirVtables();

		TEST_FUNC(Lowering.VtableEntries)
		void APointerOrANumberHoldsNoVtable();
	};
} // namespace Testing
