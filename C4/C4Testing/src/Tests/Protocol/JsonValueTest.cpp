#include "JsonValueTest.h"
#include "ObjectDescription/JsonObjectDescription.h"

using namespace Json;

namespace
{
	bool StringContains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}

namespace Testing
{
	void JsonValueTest::TestFactoriesProduceCorrectTypes()
	{
		TEST_ASSERT(Value::Null().IsNull());
		TEST_ASSERT(Value::Bool(true).IsBool());
		TEST_ASSERT(Value::Int(42).IsInt());
		TEST_ASSERT(Value::UInt(7u).IsNumber());
		TEST_ASSERT(Value::Float(1.5).IsFloat());
		TEST_ASSERT(Value::String("hi").IsString());
		TEST_ASSERT(Value::MakeObject().IsObject());
		TEST_ASSERT(Value::MakeArray().IsArray());
		TEST_PASSED();
	}

	void JsonValueTest::TestScalarGettersReturnFallbackOnTypeMismatch()
	{
		Value intVal = Value::Int(123);
		TEST_ASSERT_EQ(intVal.GetInt(), (int64)123);
		TEST_ASSERT_EQ(intVal.GetString("fallback"), std::string("fallback")); // wrong type -> fallback
		TEST_ASSERT_EQ(intVal.GetBool(true), true);                            // wrong type -> fallback

		Value boolVal = Value::Bool(false);
		TEST_ASSERT_EQ(boolVal.GetBool(), false);
		TEST_ASSERT_EQ(boolVal.GetInt(99), (int64)99);

		Value strVal = Value::String("abc");
		TEST_ASSERT_EQ(strVal.GetString(), std::string("abc"));
		TEST_PASSED();
	}

	void JsonValueTest::TestObjectMemberAccessAndMissingKeyFallback()
	{
		Value obj = Value::MakeObject();
		obj.SetMember("name", Value::String("c4"));
		obj.SetMember("count", Value::Int(3));

		TEST_ASSERT(obj.HasMember("name"));
		TEST_ASSERT(!obj.HasMember("missing"));
		TEST_ASSERT_EQ(obj.Member("name").GetString(), std::string("c4"));
		TEST_ASSERT_EQ(obj.Member("count").GetInt(), (int64)3);

		// Missing key falls back to the supplied value (default: null).
		TEST_ASSERT(obj.Member("missing").IsNull());
		TEST_ASSERT_EQ(obj.Member("missing", Value::Int(5)).GetInt(), (int64)5);
		TEST_PASSED();
	}

	void JsonValueTest::TestArrayAppendAndIndexing()
	{
		Value arr = Value::MakeArray();
		arr.Append(Value::Int(1));
		arr.Append(Value::Int(2));
		arr.Append(Value::String("three"));

		TEST_ASSERT_EQ(arr.Size(), (uint32)3);
		TEST_ASSERT_EQ(arr.At(0).GetInt(), (int64)1);
		TEST_ASSERT_EQ(arr.At(1).GetInt(), (int64)2);
		TEST_ASSERT_EQ(arr.At(2).GetString(), std::string("three"));

		// Out-of-range index returns null.
		TEST_ASSERT(arr.At(99).IsNull());
		TEST_PASSED();
	}

	void JsonValueTest::TestSerializeProducesValidJson()
	{
		Value obj = Value::MakeObject();
		obj.SetMember("a", Value::Int(1));
		obj.SetMember("b", Value::String("two"));
		std::string json = obj.Serialize();

		TEST_ASSERT(StringContains(json, "\"a\""));
		TEST_ASSERT(StringContains(json, "1"));
		TEST_ASSERT(StringContains(json, "\"two\""));

		// Re-parse and confirm the values survive.
		Value parsed = Value(Core::LoadObjectDescriptionFromJsonString(json));
		TEST_ASSERT(parsed.IsObject());
		TEST_ASSERT_EQ(parsed.Member("a").GetInt(), (int64)1);
		TEST_ASSERT_EQ(parsed.Member("b").GetString(), std::string("two"));
		TEST_PASSED();
	}

	void JsonValueTest::TestNullRoundTripsThroughJson()
	{
		Value obj = Value::MakeObject();
		obj.SetMember("value", Value::Null());
		std::string json = obj.Serialize();

		// A proper null must be emitted as the JSON literal, not an empty object.
		TEST_ASSERT(StringContains(json, "null"));

		Value parsed = Value(Core::LoadObjectDescriptionFromJsonString(json));
		TEST_ASSERT(parsed.Member("value").IsNull());
		TEST_PASSED();
	}

	void JsonValueTest::TestNestedStructureSerializesAndParsesBack()
	{
		Value array = Value::MakeArray();
		for (int i = 0; i < 3; ++i)
			array.Append(Value::Int(i));

		Value obj = Value::MakeObject();
		obj.SetMember("items", array);
		obj.SetMember("label", Value::String("nested"));

		std::string json = obj.Serialize();
		Value parsed = Value(Core::LoadObjectDescriptionFromJsonString(json));

		TEST_ASSERT(parsed.IsObject());
		Value items = parsed.Member("items");
		TEST_ASSERT(items.IsArray());
		TEST_ASSERT_EQ(items.Size(), (uint32)3);
		TEST_ASSERT_EQ(items.At(2).GetInt(), (int64)2);
		TEST_ASSERT_EQ(parsed.Member("label").GetString(), std::string("nested"));

		// In-memory (no serialize/parse round trip): a nested array must keep its elements after SetMember.
		Value changeArray = Value::MakeArray();
		changeArray.Append(Value::Int(7));
		Value paramsObj = Value::MakeObject();
		paramsObj.SetMember("contentChanges", changeArray);
		TEST_ASSERT_EQ(paramsObj.Member("contentChanges").Size(), (uint32)1);

		// Same shape as a real didChange notification: object-in-array, straight through the JSON round trip.
		Value docChange = Value::MakeObject();
		docChange.SetMember("uri", Value::String(Core::String("file:///x.cf")));
		Value fullText = Value::MakeObject();
		fullText.SetMember("text", Value::String(Core::String("void main() {}")));
		changeArray.Append(fullText); // 2 elements now: int + object (matches LspServerTest's payload)
		docChange.SetMember("contentChanges", changeArray);
		std::string didChangeJson = docChange.Serialize();
		Value parsedDoc = Value(Core::LoadObjectDescriptionFromJsonString(didChangeJson));
		TEST_ASSERT_EQ(parsedDoc.Member("contentChanges").Size(), (uint32)2);
		TEST_PASSED();
	}

	// BUG-202: the parser used to stop the process on malformed text (an assertion), so a bad message from a
	// client ended the server.
	void JsonValueTest::TestMalformedTextIsRejected()
	{
		const char* const malformed[] = {
			"", "   ", "{not json", R"({"a" 1})", R"({"a":1,})", "[1,2", R"("open)", R"("\q")", R"("\u12")",
			R"("\uZZZZ")", R"("\ud800")", R"("\ud800\u12")", R"("\ud800\uZZZZ")", R"("\ud800A")", "-", "tru",
			"nul", R"({"a":1} trailing)", "{} {}" };
		for (const char* text : malformed)
		{
			Core::ObjectDescription out;
			std::string error;
			TEST_ASSERT(!Core::TryLoadObjectDescriptionFromJsonString(text, out, &error));
			TEST_ASSERT(!error.empty());
		}
		Core::ObjectDescription out;
		TEST_ASSERT(Core::TryLoadObjectDescriptionFromJsonString(" {\"a\":[1,\"\\u00e9\\ud83d\\ude00\"]} ", out));
		TEST_ASSERT_EQ(Value(out).Member("a").Size(), (uint32)2);
		TEST_PASSED();
	}
} // namespace Testing
