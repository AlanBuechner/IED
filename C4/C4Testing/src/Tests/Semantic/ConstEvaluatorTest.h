#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Compile-time constants (ConstEvaluator): constants and enum members fold through each other in any order, every
	// operator on integers, floats and bools folds as C does, and what cannot fold says why.
	CLASS(GROUP = Testing) ConstEvaluatorTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ConstEvaluatorTest);

		TEST_FUNC(Semantic.ConstEvaluator)
		void ConstantsFoldThroughEachOtherInAnyOrder();
		TEST_FUNC(Semantic.ConstEvaluator)
		void EnumMembersCountOnFromTheLastValue();
		TEST_FUNC(Semantic.ConstEvaluator)
		void IntegerOperatorsFoldAsInC();
		TEST_FUNC(Semantic.ConstEvaluator)
		void FloatsAndBoolsFold();
		TEST_FUNC(Semantic.ConstEvaluator)
		void SizesFold();
		TEST_FUNC(Semantic.ConstEvaluator)
		void CastsAndComplementFold();
		TEST_FUNC(Semantic.ConstEvaluator)
		void ACastFoldsOnlyWhatHasAConstantValue();
		TEST_FUNC(Semantic.ConstEvaluator)
		void IntegerEdgesHaveTheRunTimesValue();
		TEST_FUNC(Semantic.ConstEvaluator)
		void AnUnresolvedEnumSaysWhy();
		TEST_FUNC(Semantic.ConstEvaluator)
		void ACycleIsReported();
		TEST_FUNC(Semantic.ConstEvaluator)
		void DivisionByZeroDoesNotFold();
		TEST_FUNC(Semantic.ConstEvaluator)
		void ARunTimeValueDoesNotFold();
	};
} // namespace Testing
