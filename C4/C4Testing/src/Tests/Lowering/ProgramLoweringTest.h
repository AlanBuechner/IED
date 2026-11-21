#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// PROGRAM LOWERING (Backend/Lowering/ProgramLowering, R2): what a program lowers and in what order, decided from the
	// lowered trees alone - written bodies, what they name, the vtables of the objects they hold, the thunks reflection
	// calls through, the globals' starts and storage - the same for C4.exe, the language server and scripts; and the
	// emitter, which lowers nothing, refusing to name what lowering did not lower.
	CLASS(GROUP = Testing) ProgramLoweringTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ProgramLoweringTest);

		TEST_FUNC(Lowering.Program)
		void AnUnusedMemberOfAnInstanceIsNotLowered();

		TEST_FUNC(Lowering.Program)
		void ACalledMemberIsLoweredWithItsUse();

		TEST_FUNC(Lowering.Program)
		void AnObjectsVtableNeedsItsVirtualMembers();

		TEST_FUNC(Lowering.Program)
		void AReflectedInstanceNeedsItsVirtualMembers();

		TEST_FUNC(Lowering.Program)
		void AGlobalsStorageNeedsItsVirtualMembers();

		TEST_FUNC(Lowering.Program)
		void AFunctionPointerGlobalStartsAtTheFunctionItNames();

		TEST_FUNC(Lowering.Program)
		void EveryStepRunsAfterAFailedBody();

		TEST_FUNC(Lowering.Program)
		void ACensusCounterIsLoweredOnce();

		TEST_FUNC(Lowering.Program)
		void TheOrderIsTheSameOnEveryThreadCount();

		TEST_FUNC(Lowering.Program)
		void CodegenRefusesToNameWhatWasNotLowered();
	};
} // namespace Testing
