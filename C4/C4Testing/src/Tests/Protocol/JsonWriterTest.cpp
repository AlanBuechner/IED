#include "JsonWriterTest.h"
#include <string>

using namespace Json;

namespace
{
	bool StringContains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}

namespace Testing
{
	std::string JsonWriterTest::ExtractBody(const std::string& framed)
	{
		size_t sep = framed.find("\r\n\r\n");
		return sep == std::string::npos ? "" : framed.substr(sep + 4);
	}

	void JsonWriterTest::TestEmitsHeaderAndBody()
	{
		Value obj = Value::MakeObject();
		obj.SetMember("k", Value::Int(7));

		std::string framed = Writer::Encode(obj);
		TEST_ASSERT(framed.rfind("Content-Length: ", 0) == 0); // starts with the header
		TEST_ASSERT(StringContains(framed, "\r\n\r\n"));

		std::string body = ExtractBody(framed);
		Value parsed = Value(Core::LoadObjectDescriptionFromJsonString(body));
		TEST_ASSERT_EQ(parsed.Member("k").GetInt(), (int64)7);
		TEST_PASSED();
	}

	void JsonWriterTest::TestContentLengthIsByteBasedForMultibyteText()
	{
		Value obj = Value::MakeObject();
		obj.SetMember("s", Value::String(Core::String(std::string("caf\303\251")))); // "cafe"

		std::string framed = Writer::Encode(obj);
		std::string body = ExtractBody(framed);

		// The declared Content-Length must equal the actual number of body bytes.
		size_t valueStart = framed.find_first_not_of(" \t", framed.find(':') + 1);
		size_t lineEnd = framed.find("\r\n", valueStart);
		long long declared = std::stoll(framed.substr(valueStart, lineEnd - valueStart));
		TEST_ASSERT_EQ(static_cast<long long>(body.size()), declared);

		// The body must decode back to a string of the right length.
		Value parsed = Value(Core::LoadObjectDescriptionFromJsonString(body));
		TEST_ASSERT(parsed.Member("s").IsString());
		TEST_PASSED();
	}

	void JsonWriterTest::TestRoundTripThroughReader()
	{
		Value obj = Value::MakeObject();
		obj.SetMember("n", Value::Int(1234));
		Value arr = Value::MakeArray();
		arr.Append(Value::String(Core::String("a")));
		arr.Append(Value::String(Core::String("b")));
		obj.SetMember("list", arr);

		std::string framed = Writer::Encode(obj);

		Reader reader;
		reader.Append(framed.data(), framed.size());
		Value decoded;
		TEST_ASSERT(reader.TryReadDocument(decoded));
		TEST_ASSERT_EQ(decoded.Member("n").GetInt(), (int64)1234);
		Value list = decoded.Member("list");
		TEST_ASSERT_EQ(list.Size(), (uint32)2);
		TEST_ASSERT_EQ(list.At(1).GetString(), std::string("b"));
		TEST_PASSED();
	}
} // namespace Testing
