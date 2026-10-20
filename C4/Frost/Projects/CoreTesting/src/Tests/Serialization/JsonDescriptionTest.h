#pragma once

#include "UnitTesting/TestBase.h"
#include "ObjectDescription/JsonObjectDescription.h"

namespace Testing
{
	CLASS(GROUP = Testing) JsonDescriptionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::JsonDescriptionTest);

		TEST_FUNC(Json)
		void TestParseString();

		TEST_FUNC(Json)
		void TestParseIntegers();

		TEST_FUNC(Json)
		void TestParseFloats();

		TEST_FUNC(Json)
		void TestParseBooleans();

		TEST_FUNC(Json)
		void TestParseArray();

		TEST_FUNC(Json)
		void TestParseObject();

		TEST_FUNC(Json)
		void TestParseEmptyObject();

		TEST_FUNC(Json)
		void TestParseNestedStructure();

		TEST_FUNC(Json)
		void TestLoadFromFile();

	};
}
