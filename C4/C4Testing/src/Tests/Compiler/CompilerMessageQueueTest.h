#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The parse scheduler's message queue (Compiler/CompilerMessageQueue): a parser thread waiting on an include takes
	// its response only once the scheduler releases it (FlushPendingResponse), never when it is merely pushed (BUG-436).
	CLASS(GROUP = Testing) CompilerMessageQueueTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CompilerMessageQueueTest);

		TEST_FUNC(Compiler.MessageQueue)
		void AWaiterTakesAReleasedResponse();
		TEST_FUNC(Compiler.MessageQueue)
		void APushedResponseIsNotTakenBeforeItIsReleased();
		TEST_FUNC(Compiler.MessageQueue)
		void ReleasingWithNothingPushedDoesNothing();
		TEST_FUNC(Compiler.MessageQueue)
		void AResponseGoesOnlyToItsOwnSource();
	};
} // namespace Testing
