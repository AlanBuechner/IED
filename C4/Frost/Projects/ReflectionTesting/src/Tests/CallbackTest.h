#pragma once

#include "UnitTesting/TestBase.h"
#include "Reflection.h"
#include "ReflectionEvents.h"

namespace Testing
{
	CLASS(GROUP = Testing) CallbackTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CallbackTest);

		TEST_FUNC(Callback)
		void TestHandleUniqueness();

		TEST_FUNC(Callback)
		void TestUnloadCallbackInvocation();

		TEST_FUNC(Callback)
		void TestReloadCallbackInvocation();

		TEST_FUNC(Callback)
		void TestUnsubscribeRemovesCallback();

		TEST_FUNC(Callback)
		void TestInvocationOrder();
	};
}
