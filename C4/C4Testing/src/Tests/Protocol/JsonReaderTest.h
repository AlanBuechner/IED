#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Json/JsonReader.h"

namespace Testing
{
	// Base-protocol framing (LSP and DAP): Content-Length header parsing, buffering of partial frames across
	// stdin chunks, multi-frame reads, and dropping the frames it cannot use.
	CLASS(GROUP = Testing) JsonReaderTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::JsonReaderTest);

		TEST_FUNC(Protocol.JsonReader)
		void TestReadsSingleCompleteFrame();

		TEST_FUNC(Protocol.JsonReader)
		void TestBuffersPartialHeaderAcrossChunks();

		TEST_FUNC(Protocol.JsonReader)
		void TestBuffersPartialBodyAcrossChunks();

		TEST_FUNC(Protocol.JsonReader)
		void TestReadsMultipleFramesFromOneChunk();

		TEST_FUNC(Protocol.JsonReader)
		void TestIgnoresExtraHeaderFields();

		TEST_FUNC(Protocol.JsonReader)
		void TestHeaderWithoutLengthIsDropped();

		TEST_FUNC(Protocol.JsonReader)
		void TestImpossibleLengthIsDropped();

		TEST_FUNC(Protocol.JsonReader)
		void TestMalformedBodyDoesNotHideTheNextFrame();

	private:
		std::string Frame(const std::string& body);
	};
} // namespace Testing
