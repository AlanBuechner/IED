#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Pass 2 of the model build (DeclarationQueries): a declaration resolves what it names first, whatever the order
	// they are written in; one that depends on itself says so with the cycle; one that fails is reported once.
	CLASS(GROUP = Testing) DeclarationResolutionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DeclarationResolutionTest);

		TEST_FUNC(Semantic.DeclarationResolution)
		void ADeclarationResolvesWhatItNamesFirst();
		TEST_FUNC(Semantic.DeclarationResolution)
		void FieldsKeepTheirDeclarationOrder();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AnEnumIsMeasuredWithItsUnderlyingType();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AUnionIsMeasuredWithItsMembers();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AStaticMemberIsFoundBeforeItsClassResolves();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AStaticMemberHidesAGlobalOfItsName();
		TEST_FUNC(Semantic.DeclarationResolution)
		void ALongChainResolvesInOneSweep();
		TEST_FUNC(Semantic.DeclarationResolution)
		void MethodRecordsFollowDeclarationOrder();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AnAliasCycleNamesItsCycle();
		TEST_FUNC(Semantic.DeclarationResolution)
		void ALayoutCycleNamesItsCycle();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AnEnumCycleNamesItsCycle();
		TEST_FUNC(Semantic.DeclarationResolution)
		void ACycleThroughAConstantNamesIt();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AFailedDeclarationIsReportedOnceAndItsUsersSayWhich();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AnUnresolvedLaneViewIsReported();
		TEST_FUNC(Semantic.DeclarationResolution)
		void ATemplateWithoutALastInstanceIsAnError();
		TEST_FUNC(Semantic.DeclarationResolution)
		void ADeepButEndingInstantiationWorks();
		TEST_FUNC(Semantic.DeclarationResolution)
		void BodiesOfInstancesAreOpenedHoweverDeepTheChain();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AnInstancesExtentFoldsAConstantOfItsTemplatesScope();
		TEST_FUNC(Semantic.DeclarationResolution)
		void AnInstancesStaticConstantFolds();
	};
} // namespace Testing
