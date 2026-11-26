#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// When a waiting build server ends (Serve/ServerWait, docs/compiler/07 "The server's memory"), and the memory it
	// weighs and gives back (Compiler/ProcessMemory).
	CLASS(GROUP = Testing) ServerWaitTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ServerWaitTest);

		TEST_FUNC(Serve.Wait)
		void AServerWaitsUntilItsIdleTime();
		TEST_FUNC(Serve.Wait)
		void AServerWhoseFolderWentEndsAtOnce();
		TEST_FUNC(Serve.Wait)
		void AServerKeepsItsMemoryWhileTheMachineHasMore();
		TEST_FUNC(Serve.Wait)
		void AServerGivesItsMemoryBackAfterAWhileWhenTheMachineHasLess();
		TEST_FUNC(Serve.Wait)
		void TheTighterOfPhysicalMemoryAndCommitDecides();
		TEST_FUNC(Serve.Wait)
		void AMachineThatDoesNotSayKeepsTheServer();
		TEST_FUNC(Serve.Wait)
		void TheIdleTimeEndsAServerBeforeTheMemoryCheck();
		TEST_FUNC(Serve.Wait)
		void AServerThatOutgrewItsFirstBuildEndsAtOnce();
		TEST_FUNC(Serve.Wait)
		void AServerWithoutABuildCannotOutgrowOne();

		TEST_FUNC(Serve.Memory)
		void TheMachineAndThisProcessHaveMemory();
		TEST_FUNC(Serve.Memory)
		void FreedMemoryIsTakenAgainByTheNextAllocations();
#ifndef _WIN32
		// glibc's heap trims; on Windows the compiler allocates through LLVM's rpmalloc, which keeps what is freed for the process.
		TEST_FUNC(Serve.Memory)
		void FreedMemoryGoesBackToTheSystem();
#endif
	};
} // namespace Testing
