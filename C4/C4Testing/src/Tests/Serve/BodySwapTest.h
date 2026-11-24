#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A build server's body edit (Serve/BodyEdits, Serve/BodySwap, ProgramLowering::Relower; docs/compiler/07 stage 2a):
	// which edits change only bodies their own functions read, and that the program lowered again from such an edit emits
	// the cold build's IR, partition for partition - or the edit is refused with why.
	CLASS(GROUP = Testing) BodySwapTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BodySwapTest);

		TEST_FUNC(Serve.BodyEdits)
		void AStatementEditIsABodyEdit();
		TEST_FUNC(Serve.BodyEdits)
		void ALongerStatementOnItsLineIsABodyEdit();
		TEST_FUNC(Serve.BodyEdits)
		void AnAddedLineMovesWhatFollows();
		TEST_FUNC(Serve.BodyEdits)
		void ADeclarationEditIsNone();
		TEST_FUNC(Serve.BodyEdits)
		void AnEditOutsideTheBodiesIsNone();
		TEST_FUNC(Serve.BodyEdits)
		void ALambdaOrALocalTypeInAChangedBodyIsNone();
		TEST_FUNC(Serve.BodyEdits)
		void ABodyThatDoesNotParseIsNone();
		TEST_FUNC(Serve.BodyEdits)
		void AnInlineBodyIsNone();

		TEST_FUNC(Serve.BodySwap)
		void AStatementEditEmitsTheColdBuild();
		TEST_FUNC(Serve.BodySwap)
		void AnAddedLineEmitsTheColdBuild();
		TEST_FUNC(Serve.BodySwap)
		void ACallOfAnotherWrittenFunctionEmitsTheColdBuild();
		TEST_FUNC(Serve.BodySwap)
		void AnInstanceMadeEarlierEmitsTheColdBuild();
		TEST_FUNC(Serve.BodySwap)
		void ANewInstanceIsRefused();
		TEST_FUNC(Serve.BodySwap)
		void AnInstanceMadeInALaterPassIsRefused();
		TEST_FUNC(Serve.BodySwap)
		void AnInstanceOnlyTheOldBodyAskedForIsRefused();
		TEST_FUNC(Serve.BodySwap)
		void ABodyWithErrorsIsRefused();
		TEST_FUNC(Serve.BodySwap)
		void AWarningIsReportedAsTheColdBuildReportsIt();
		TEST_FUNC(Serve.BodySwap)
		void AnEditEmitsOnlyThePartitionsItChanges();
		TEST_FUNC(Serve.BodySwap)
		void TwoEditsInARowEmitTheColdBuild();
	};
} // namespace Testing
