#include "JsonReaderTest.h"
#include <string>

using namespace Json;

namespace Testing
{
	std::string JsonReaderTest::Frame(const std::string& body)
	{
		return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	}

	void JsonReaderTest::TestReadsSingleCompleteFrame()
	{
		std::string framed = Frame(R"({"a":1})");
		Reader reader;
		reader.Append(framed.data(), framed.size());

		Value doc;
		TEST_ASSERT(reader.TryReadDocument(doc));
		TEST_ASSERT(doc.IsObject());
		TEST_ASSERT_EQ(doc.Member("a").GetInt(), (int64)1);
		TEST_ASSERT(!reader.TryReadDocument(doc)); // nothing left
		TEST_PASSED();
	}

	void JsonReaderTest::TestBuffersPartialHeaderAcrossChunks()
	{
		std::string framed = Frame(R"({"x":5})");
		Reader reader;

		// Feed only part of the header: no full frame yet.
		reader.Append(framed.data(), 10);
		Value doc;
		TEST_ASSERT(!reader.TryReadDocument(doc));

		// Feed the rest.
		reader.Append(framed.data() + 10, framed.size() - 10);
		TEST_ASSERT(reader.TryReadDocument(doc));
		TEST_ASSERT_EQ(doc.Member("x").GetInt(), (int64)5);
		TEST_PASSED();
	}

	void JsonReaderTest::TestBuffersPartialBodyAcrossChunks()
	{
		std::string framed = Frame(R"({"long":"value"})");
		size_t headerEnd = framed.find("\r\n\r\n") + 4; // after the blank line
		Reader reader;

		// Feed header plus only part of the body.
		reader.Append(framed.data(), headerEnd + 3);
		Value doc;
		TEST_ASSERT(!reader.TryReadDocument(doc)); // body incomplete

		// Feed the remaining body bytes.
		reader.Append(framed.data() + headerEnd + 3, framed.size() - (headerEnd + 3));
		TEST_ASSERT(reader.TryReadDocument(doc));
		TEST_ASSERT_EQ(doc.Member("long").GetString(), std::string("value"));
		TEST_PASSED();
	}

	void JsonReaderTest::TestReadsMultipleFramesFromOneChunk()
	{
		std::string chunk = Frame(R"({"i":0})") + Frame(R"({"i":1})") + Frame(R"({"i":2})");
		Reader reader;
		reader.Append(chunk.data(), chunk.size());

		for (int expected = 0; expected < 3; ++expected)
		{
			Value doc;
			TEST_ASSERT(reader.TryReadDocument(doc));
			TEST_ASSERT_EQ(doc.Member("i").GetInt(), (int64)expected);
		}
		Value extra;
		TEST_ASSERT(!reader.TryReadDocument(extra));
		TEST_PASSED();
	}

	void JsonReaderTest::TestIgnoresExtraHeaderFields()
	{
		std::string body = R"({"ok":true})";
		std::string framed = "Content-Type: application/vscode-jsonrpc\r\nContent-Length: " +
			std::to_string(body.size()) + "\r\n\r\n" + body;

		Reader reader;
		reader.Append(framed.data(), framed.size());
		Value doc;
		TEST_ASSERT(reader.TryReadDocument(doc));
		TEST_ASSERT_EQ(doc.Member("ok").GetBool(), true);
		TEST_PASSED();
	}

	// BUG-202: the frame after a header with no length used to be unreachable.
	void JsonReaderTest::TestHeaderWithoutLengthIsDropped()
	{
		std::string chunk = "Content-Type: text\r\n\r\n" + Frame(R"({"after":1})");
		Reader reader;
		reader.Append(chunk.data(), chunk.size());
		Value doc;
		TEST_ASSERT(reader.TryReadDocument(doc));
		TEST_ASSERT_EQ(doc.Member("after").GetInt(), (int64)1);
		TEST_ASSERT_EQ(reader.BufferedBytes(), (size_t)0);
		TEST_PASSED();
	}

	// BUG-202: a length past what size_t holds used to throw out of the reader.
	void JsonReaderTest::TestImpossibleLengthIsDropped()
	{
		const std::string lengths[] = { "99999999999999999999999999", "12abc", "", "-5", std::to_string(Reader::kMaxContentLength + 1) };
		for (const std::string& length : lengths)
		{
			std::string chunk = "Content-Length: " + length + "\r\n\r\n" + Frame(R"({"next":2})");
			Reader reader;
			reader.Append(chunk.data(), chunk.size());
			Value doc;
			TEST_ASSERT(reader.TryReadDocument(doc));
			TEST_ASSERT_EQ(doc.Member("next").GetInt(), (int64)2);
		}
		TEST_PASSED();
	}

	// BUG-202: a body that is not JSON used to end the read, leaving complete frames behind it unread.
	void JsonReaderTest::TestMalformedBodyDoesNotHideTheNextFrame()
	{
		std::string chunk = Frame("{not json") + Frame(R"({"good":3})");
		Reader reader;
		reader.Append(chunk.data(), chunk.size());
		Value doc;
		TEST_ASSERT(reader.TryReadDocument(doc));
		TEST_ASSERT_EQ(doc.Member("good").GetInt(), (int64)3);
		TEST_ASSERT(!reader.TryReadDocument(doc));
		TEST_PASSED();
	}
} // namespace Testing
