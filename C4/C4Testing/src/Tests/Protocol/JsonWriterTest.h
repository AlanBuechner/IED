#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Json/JsonWriter.h"
#include "Json/JsonReader.h"

namespace Testing
{
	// Exercises the wire encoder: correct Content-Length (byte-based, so multi-byte UTF-8 is
	// counted correctly), header format, and full encode->decode round-trips.
	CLASS(GROUP = Testing) JsonWriterTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::JsonWriterTest);

		TEST_FUNC(Protocol.JsonWriter)
		void TestEmitsHeaderAndBody();

		TEST_FUNC(Protocol.JsonWriter)
		void TestContentLengthIsByteBasedForMultibyteText();

		TEST_FUNC(Protocol.JsonWriter)
		void TestRoundTripThroughReader();

	private:
		std::string ExtractBody(const std::string& framed);
	};
} // namespace Testing
