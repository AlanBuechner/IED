#include "ErrorTrackerTest.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		SourceLocation At(uint32 line)
		{
			SourceLocation location;
			location.line = line;
			location.column = 1;
			return location;
		}
	}

	void ErrorTrackerTest::ErrorsKeepTheirOrderAndPlace()
	{
		ErrorTracker errors;
		TEST_ASSERT(!errors.HasErrors());
		errors.AddError(At(3), "first");
		errors.AddError(At(1), "second", ErrorSeverity::Fatal);
		TEST_ASSERT(errors.HasErrors());
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 2u);
		TEST_ASSERT_EQ(errors.GetErrors()[0].message, Core::String("first"));
		TEST_ASSERT_EQ(errors.GetErrors()[0].location.line, 3u);
		TEST_ASSERT(errors.GetErrors()[1].severity == ErrorSeverity::Fatal);
		TEST_PASSED();
	}

	void ErrorTrackerTest::WarningsAloneAreNotFailures()
	{
		ErrorTracker errors;
		errors.AddError(At(1), "unused", ErrorSeverity::Warning);
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 1u);
		TEST_ASSERT(!errors.HasErrors());
		errors.AddError(At(2), "bad");
		TEST_ASSERT(errors.HasErrors());
		TEST_PASSED();
	}

	void ErrorTrackerTest::AnEmptyMessageIsDropped()
	{
		ErrorTracker errors;
		errors.AddError(At(1), "");
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 0u);
		TEST_ASSERT(!errors.HasErrors());
		TEST_PASSED();
	}

	void ErrorTrackerTest::ARolledBackAttemptLeavesNoDiagnostics()
	{
		ErrorTracker errors;
		errors.AddError(At(1), "before");
		errors.SuppressErrors();
		TEST_ASSERT(errors.AreErrorsSuppressed());
		TEST_ASSERT(!errors.SpeculationFailed());
		errors.AddError(At(2), "probe");
		TEST_ASSERT(errors.SpeculationFailed());
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 1u); // buffered, not reported yet
		errors.DiscardSuppressedErrors();
		TEST_ASSERT(!errors.AreErrorsSuppressed());
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 1u);
		TEST_ASSERT_EQ(errors.GetErrors()[0].message, Core::String("before"));
		TEST_PASSED();
	}

	void ErrorTrackerTest::ACommittedAttemptKeepsItsDiagnostics()
	{
		ErrorTracker errors;
		errors.SuppressErrors();
		errors.AddError(At(4), "inside the kept body");
		errors.CommitSuppressedErrors();
		TEST_ASSERT(!errors.AreErrorsSuppressed());
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 1u);
		TEST_ASSERT_EQ(errors.GetErrors()[0].location.line, 4u);
		TEST_PASSED();
	}

	void ErrorTrackerTest::NestedAttemptsCommitIntoTheirParent()
	{
		ErrorTracker errors;
		errors.SuppressErrors();
		errors.AddError(At(1), "outer");
		errors.SuppressErrors();
		TEST_ASSERT(!errors.SpeculationFailed()); // the inner attempt has its own frame
		errors.AddError(At(2), "inner kept");
		errors.CommitSuppressedErrors();          // into the outer frame, not the list
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 0u);
		TEST_ASSERT(errors.AreErrorsSuppressed());
		errors.SuppressErrors();
		errors.AddError(At(3), "inner dropped");
		errors.DiscardSuppressedErrors();
		errors.CommitSuppressedErrors();
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 2u);
		TEST_ASSERT_EQ(errors.GetErrors()[0].message, Core::String("outer"));
		TEST_ASSERT_EQ(errors.GetErrors()[1].message, Core::String("inner kept"));
		TEST_PASSED();
	}

	void ErrorTrackerTest::UnbalancedEndsAndClearAreHarmless()
	{
		ErrorTracker errors;
		errors.CommitSuppressedErrors();
		errors.DiscardSuppressedErrors();
		TEST_ASSERT(!errors.AreErrorsSuppressed());
		errors.AddError(At(1), "kept");
		errors.SuppressErrors();
		errors.SuppressErrors();
		errors.AddError(At(2), "probe");
		errors.Clear(); // drops the list and every open frame
		TEST_ASSERT(!errors.AreErrorsSuppressed());
		TEST_ASSERT(!errors.SpeculationFailed());
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 0u);
		errors.AddError(At(3), "after");
		TEST_ASSERT_EQ(errors.GetErrors().Count(), 1u);
		TEST_PASSED();
	}
} // namespace Testing
