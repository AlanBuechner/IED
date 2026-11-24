#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Stack maps (R16 phase 3, Compiler/StackMaps): without -fstack-maps the module is what it always was, and with it the
	// optimizer sees the same functions; afterwards each call lists the values live across it that may hold an address (a
	// value made from another listed one, a counter and a stack slot's address are none) and the frame's slots live there,
	// an invoke too; only a call that may become a sibling call and a C library call code generation expands keep none;
	// many values at one call still fit the frame, an
	// ELF object's stack map is ready for the link, and only such a program links the runtime built with them.
	CLASS(GROUP = Testing) StackMapsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StackMapsTest);

		TEST_FUNC(Compiler.StackMaps)
		void WithoutTheFlagNothingChanges();
		TEST_FUNC(Compiler.StackMaps)
		void TheFlagChangesNoFunction();
		TEST_FUNC(Compiler.StackMaps)
		void CallsGetRecords();
		TEST_FUNC(Compiler.StackMaps)
		void ADeadSlotIsNotListed();
		TEST_FUNC(Compiler.StackMaps)
		void AValueLiveAcrossACallIsListed();
		TEST_FUNC(Compiler.StackMaps)
		void ACounterIsNoAddress();
		TEST_FUNC(Compiler.StackMaps)
		void OnlyASiblingCallKeepsNoRecord();
		TEST_FUNC(Compiler.StackMaps)
		void AnInvokeGetsARecord();
		TEST_FUNC(Compiler.StackMaps)
		void ManyValuesAtOneCallFitTheFrame();
		TEST_FUNC(Compiler.StackMaps)
		void AnElfStackMapIsWritableAndBracketed();
		TEST_FUNC(Compiler.StackMaps)
		void ALibraryCallCodeGenerationExpandsHasNoRecord();
		TEST_FUNC(Compiler.StackMaps)
		void OnlyAProgramWithStackMapsLinksTheirRuntime();
	};
} // namespace Testing
