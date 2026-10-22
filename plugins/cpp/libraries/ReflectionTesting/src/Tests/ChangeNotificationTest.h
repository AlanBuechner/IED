#pragma once

#include "UnitTesting/TestBase.h"
#include "ChangeNotification/ChangeNotification.h"

namespace Testing
{
	CLASS(GROUP = Testing) ChangeNotificationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ChangeNotificationTest);

		// ObservableProperty emission behavior
		TEST_FUNC(ChangeNotification)
		void TestObservablePropertyEmission();

		// Duplicate suppression - setting to the same value emits nothing
		TEST_FUNC(ChangeNotification)
		void TestDuplicateSuppression();

		// Subscription and unsubscription lifecycle
		TEST_FUNC(ChangeNotification)
		void TestSubscribeUnsubscribeLifecycle();

		// Exact path filter matching
		TEST_FUNC(ChangeNotification)
		void TestExactPathFilter();

		// Wildcard path filter matching (* and ?)
		TEST_FUNC(ChangeNotification)
		void TestWildcardPathFilter();

		// Batch accumulation mode
		TEST_FUNC(ChangeNotification)
		void TestBatchAccumulation();

		// ProcessPendingNotifications drain
		TEST_FUNC(ChangeNotification)
		void TestPendingDrain();
	};
}
