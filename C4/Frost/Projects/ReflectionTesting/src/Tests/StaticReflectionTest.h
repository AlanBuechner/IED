#pragma once

#include "UnitTesting/TestBase.h"
#include "Reflection.h"
#include "ReflectionEvents.h"
#include "StaticReflection/ReflectionData.h"
#include "ObjectDescription/ObjectDescription.h"

namespace Testing
{
	CLASS(GROUP = Testing) StaticReflectionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StaticReflectionTest);

		TEST_FUNC(StaticReflection)
		void TestPropertySerializationRoundTrip();

		TEST_FUNC(StaticReflection)
		void TestFunctionSerializationRoundTrip();

		TEST_FUNC(StaticReflection)
		void TestClassSerializationRoundTrip();

		TEST_FUNC(StaticReflection)
		void TestStaticClassBackwardCompatibility();
	};
}
