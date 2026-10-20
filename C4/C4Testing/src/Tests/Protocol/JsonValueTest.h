#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Json/JsonValue.h"

namespace Testing
{
	// Exercises the Json::Value wrapper: factories, type queries, scalar/object/array access and
	// the round-trip through the ObjectDescription JSON serializer.
	CLASS(GROUP = Testing) JsonValueTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::JsonValueTest);

		TEST_FUNC(Protocol.JsonValue)
		void TestFactoriesProduceCorrectTypes();

		TEST_FUNC(Protocol.JsonValue)
		void TestScalarGettersReturnFallbackOnTypeMismatch();

		TEST_FUNC(Protocol.JsonValue)
		void TestObjectMemberAccessAndMissingKeyFallback();

		TEST_FUNC(Protocol.JsonValue)
		void TestArrayAppendAndIndexing();

		TEST_FUNC(Protocol.JsonValue)
		void TestSerializeProducesValidJson();

		TEST_FUNC(Protocol.JsonValue)
		void TestNullRoundTripsThroughJson();

		TEST_FUNC(Protocol.JsonValue)
		void TestNestedStructureSerializesAndParsesBack();

		TEST_FUNC(Protocol.JsonValue)
		void TestMalformedTextIsRejected();
	};
} // namespace Testing
