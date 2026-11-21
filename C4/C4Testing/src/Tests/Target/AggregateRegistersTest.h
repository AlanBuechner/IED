#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The registers each calling convention gives a by-value aggregate (Target/AggregateRegisters.h): Windows x64 by its
	// size, System V by the scalars of each eightbyte, as clang classifies them.
	CLASS(GROUP = Testing) AggregateRegistersTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AggregateRegistersTest);

		TEST_FUNC(Target.Aggregates)
		void Win64PassesOneRegisterSizesAsAnInteger();
		TEST_FUNC(Target.Aggregates)
		void Win64PassesEveryOtherSizeInMemory();
		TEST_FUNC(Target.Aggregates)
		void Win64NeverLooksAtTheFields();
		TEST_FUNC(Target.Aggregates)
		void SystemVPassesFloatsInSseRegisters();
		TEST_FUNC(Target.Aggregates)
		void SystemVMixesIntegersIntoAnIntegerEightbyte();
		TEST_FUNC(Target.Aggregates)
		void SystemVClassifiesEachEightbyteAlone();
		TEST_FUNC(Target.Aggregates)
		void SystemVPassesLargeOrUnalignedAggregatesInMemory();
		TEST_FUNC(Target.Aggregates)
		void EachTargetUsesItsConvention();
	};
} // namespace Testing
