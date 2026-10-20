#pragma once

#include "UnitTesting/TestBase.h"
#include "Reflection.h"
#include "ReflectionEvents.h"
#include "ObjectDescription/ObjectDescription.h"

namespace Testing
{
	CLASS(GROUP = Testing) ReloadTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ReloadTest);

		TEST_FUNC(Reload)
		void TestSerializeBeforeUnload();

		TEST_FUNC(Reload)
		void TestRestoreAfterRegistration();

		TEST_FUNC(Reload)
		void TestStaticClassesUnaffectedByUnload();

		TEST_FUNC(Reload)
		void TestFullReloadCycle();
	};
}
