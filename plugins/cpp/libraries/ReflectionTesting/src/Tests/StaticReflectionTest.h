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

		// The HeaderTool's attribute strings end in a value ("reflect-class,GROUP = Testing"): the parser read the token
		// after it, past the end of its queue (a crash with libstdc++ on Linux).
		TEST_FUNC(StaticReflection)
		void TestAttributeStringsEndingInAValueOrAnEquals();
	};
}
