#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Stack maps (R16 phase 3, Compiler/StackMaps): without -fstack-maps the module is what it always was; with it every C4
	// pointer is in the managed address space, the calls become statepoints that list the frame's slots live across them,
	// a call RewriteStatepointsForGC cannot take stays a leaf, the optimizer keeps a library call that takes managed pointers
	// as written, an ELF object's stack map is ready for the link, and only such a program links the runtime built with them.
	CLASS(GROUP = Testing) StackMapsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StackMapsTest);

		TEST_FUNC(Compiler.StackMaps)
		void WithoutTheFlagNothingChanges();
		TEST_FUNC(Compiler.StackMaps)
		void ManagedPointersHaveTheirOwnSpace();
		TEST_FUNC(Compiler.StackMaps)
		void CallsBecomeStatepoints();
		TEST_FUNC(Compiler.StackMaps)
		void ADeadSlotIsNotListed();
		TEST_FUNC(Compiler.StackMaps)
		void InvokesStayLeaves();
		TEST_FUNC(Compiler.StackMaps)
		void AnElfStackMapIsWritableAndBracketed();
		TEST_FUNC(Compiler.StackMaps)
		void ALibraryCallKeepsItsManagedPointers();
		TEST_FUNC(Compiler.StackMaps)
		void OnlyAProgramWithStackMapsLinksTheirRuntime();
	};
} // namespace Testing
