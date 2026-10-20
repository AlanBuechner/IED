#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Model/OverloadResolution.h"
#include "Model/Types.h"

namespace Testing
{
	CLASS(GROUP = Testing) OverloadResolutionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::OverloadResolutionTest);

		TEST_FUNC(Semantic.OverloadResolution)
		void TestSingleCandidateExactMatch();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestPicksExactOverConverted();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestFewerTotalConversionsWins();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestDefaultedTailViaMinArity();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestTooManyArgsForCandidateSkipsIt();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestAmbiguousWhenTied();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestNoMatchWhenNothingViable();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestEmptyCandidateSetIsNoMatch();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestReferenceParamBindsByExactReferent();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestEnumConvertsToUnderlyingInt();

		TEST_FUNC(Semantic.OverloadResolution)
		void TestAliasUnwrapsForMatching();
	};
} // namespace Testing
