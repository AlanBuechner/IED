#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What the server does with the messages that arrive while a request waits for an analysis (docs/tools/03 "Requests
	// while it analyzes", Server/MessageInbox): BUG-466, the server read the next message only once it had answered the
	// last, ignored $/cancelRequest and counted a request's budget from when it took it - so after a burst of typing each
	// edit cost an analysis and each request (cancelled ones too) a wait, for minutes, and IED gave up on them at 30 s.
	CLASS(GROUP = Testing) LspInboxTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspInboxTest);

		TEST_FUNC(Lsp.Inbox)
		void TestMessagesComeInTheOrderTheyArrived();

		TEST_FUNC(Lsp.Inbox)
		void TestACancellationMarksItsRequestOnArrival();

		TEST_FUNC(Lsp.Inbox)
		void TestAnEditOfADocumentIsSeenUntilTaken();

		TEST_FUNC(Lsp.Inbox)
		void TestEachArrivalWakesWhoListens();

		TEST_FUNC(Lsp.Inbox)
		void TestACancelledRequestAnswersAtOnce();

		TEST_FUNC(Lsp.Inbox)
		void TestARequestBeforeAnEditOfItsDocumentAnswersContentModified();

		TEST_FUNC(Lsp.Inbox)
		void TestAnEditOfAnotherDocumentLeavesARequestWaiting();

		TEST_FUNC(Lsp.Inbox)
		void TestARequestsBudgetCountsFromItsArrival();

		TEST_FUNC(Lsp.Inbox)
		void TestAnAnsweredRequestsCancellationIsForgotten();

		// Twenty edits with the requests an editor makes for each: one analysis, the outdated requests answered at once.
		TEST_FUNC(Lsp.Inbox)
		void TestABurstOfTypingCostsOneAnalysis();

		TEST_FUNC(Lsp.Inbox)
		void TestServingEndsAtExit();
	};
} // namespace Testing
