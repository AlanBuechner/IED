#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The compiler's diagnostics list: severities, clearing, and the suppression frames speculative parsing uses (a
	// committed attempt keeps its diagnostics, a rolled-back one drops them, frames nest).
	CLASS(GROUP = Testing) ErrorTrackerTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ErrorTrackerTest);

		TEST_FUNC(Compiler.ErrorTracker)
		void ErrorsKeepTheirOrderAndPlace();
		TEST_FUNC(Compiler.ErrorTracker)
		void WarningsAloneAreNotFailures();
		TEST_FUNC(Compiler.ErrorTracker)
		void AnEmptyMessageIsDropped();
		TEST_FUNC(Compiler.ErrorTracker)
		void ARolledBackAttemptLeavesNoDiagnostics();
		TEST_FUNC(Compiler.ErrorTracker)
		void ACommittedAttemptKeepsItsDiagnostics();
		TEST_FUNC(Compiler.ErrorTracker)
		void NestedAttemptsCommitIntoTheirParent();
		TEST_FUNC(Compiler.ErrorTracker)
		void UnbalancedEndsAndClearAreHarmless();
	};
} // namespace Testing
