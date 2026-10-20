#include "JsonDescriptionTest.h"

namespace Testing
{
	void JsonDescriptionTest::TestParseString()
	{
		Core::ObjectDescription desc = Core::LoadObjectDescriptionFromJsonString(R"("hello world")");

		if (!desc.IsString())
			TEST_FAILED("Expected string type, got type {0}", static_cast<int>(desc.GetType()));
		if (desc.GetAsString() != "hello world")
		{
			Core::String msg = Core::FormatString("Expected 'hello world', got '{0}'", desc.GetAsString());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseIntegers()
	{
		// Positive unsigned integer
		Core::ObjectDescription pos = Core::LoadObjectDescriptionFromJsonString("42");
		if (!pos.IsUint())
			TEST_FAILED("Expected uint type for positive integer, got type {0}", static_cast<int>(pos.GetType()));
		if (pos.GetAsUint() != 42)
		{
			Core::String msg = Core::FormatString("Expected 42, got {0}", pos.GetAsUint());
			TEST_FAILED(msg);
		}

		// Negative signed integer
		Core::ObjectDescription neg = Core::LoadObjectDescriptionFromJsonString("-17");
		if (!neg.IsSint())
			TEST_FAILED("Expected sint type for negative integer, got type {0}", static_cast<int>(neg.GetType()));
		if (neg.GetAsInt() != -17)
		{
			Core::String msg = Core::FormatString("Expected -17, got {0}", neg.GetAsInt());
			TEST_FAILED(msg);
		}

		// Zero
		Core::ObjectDescription zero = Core::LoadObjectDescriptionFromJsonString("0");
		if (!zero.IsUint())
			TEST_FAILED("Expected uint type for zero, got type {0}", static_cast<int>(zero.GetType()));
		if (zero.GetAsUint() != 0)
		{
			Core::String msg = Core::FormatString("Expected 0, got {0}", zero.GetAsUint());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseFloats()
	{
		// Positive float with decimal
		Core::ObjectDescription f1 = Core::LoadObjectDescriptionFromJsonString("3.14");
		if (!f1.IsFloat())
			TEST_FAILED("Expected float type, got type {0}", static_cast<int>(f1.GetType()));
		if (std::abs(f1.GetAsFloat() - 3.14) > 0.001)
		{
			Core::String msg = Core::FormatString("Expected 3.14, got {0}", f1.GetAsFloat());
			TEST_FAILED(msg);
		}

		// Negative float
		Core::ObjectDescription f2 = Core::LoadObjectDescriptionFromJsonString("-0.5");
		if (!f2.IsFloat())
			TEST_FAILED("Expected float type for negative float, got type {0}", static_cast<int>(f2.GetType()));
		if (std::abs(f2.GetAsFloat() - (-0.5)) > 0.001)
		{
			Core::String msg = Core::FormatString("Expected -0.5, got {0}", f2.GetAsFloat());
			TEST_FAILED(msg);
		}

		// Scientific notation
		Core::ObjectDescription f3 = Core::LoadObjectDescriptionFromJsonString("1.5e2");
		if (!f3.IsFloat())
			TEST_FAILED("Expected float type for scientific notation, got type {0}", static_cast<int>(f3.GetType()));
		if (std::abs(f3.GetAsFloat() - 150.0) > 0.001)
		{
			Core::String msg = Core::FormatString("Expected 150.0, got {0}", f3.GetAsFloat());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseBooleans()
	{
		Core::ObjectDescription t = Core::LoadObjectDescriptionFromJsonString("true");
		if (!t.IsBool())
			TEST_FAILED("Expected bool type for true, got type {0}", static_cast<int>(t.GetType()));
		if (!t.GetAsBool())
			TEST_FAILED("Expected true, got false");

		Core::ObjectDescription f = Core::LoadObjectDescriptionFromJsonString("false");
		if (!f.IsBool())
			TEST_FAILED("Expected bool type for false, got type {0}", static_cast<int>(f.GetType()));
		if (f.GetAsBool())
			TEST_FAILED("Expected false, got true");

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseArray()
	{
		Core::ObjectDescription arr = Core::LoadObjectDescriptionFromJsonString("[1, \"two\", true]");

		if (!arr.IsArray())
			TEST_FAILED("Expected array type, got type {0}", static_cast<int>(arr.GetType()));
		if (arr.GetAsObjectArray().Count() != 3)
		{
			Core::String msg = Core::FormatString("Expected 3 elements, got {0}", arr.GetAsObjectArray().Count());
			TEST_FAILED(msg);
		}

		if (!arr[0].IsUint() || arr[0].GetAsUint() != 1)
			TEST_FAILED("First element should be uint 1");
		if (!arr[1].IsString() || arr[1].GetAsString() != "two")
			TEST_FAILED("Second element should be string 'two'");
		if (!arr[2].IsBool() || !arr[2].GetAsBool())
			TEST_FAILED("Third element should be bool true");

		// Empty array
		Core::ObjectDescription empty = Core::LoadObjectDescriptionFromJsonString("[]");
		if (!empty.IsArray())
			TEST_FAILED("Expected array type for empty array");
		if (empty.GetAsObjectArray().Count() != 0)
			TEST_FAILED("Empty array should have 0 elements");

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseObject()
	{
		Core::ObjectDescription obj = Core::LoadObjectDescriptionFromJsonString(R"({"key": "value", "num": 42})");

		if (!obj.IsObject())
			TEST_FAILED("Expected object type, got type {0}", static_cast<int>(obj.GetType()));

		const auto& map = obj.GetAsDescriptionMap();
		if (map.size() != 2)
		{
			Core::String msg = Core::FormatString("Expected 2 entries, got {0}", map.size());
			TEST_FAILED(msg);
		}

		if (!obj["key"].IsString() || obj["key"].GetAsString() != "value")
			TEST_FAILED("Expected key='value'");
		if (!obj["num"].IsUint() || obj["num"].GetAsUint() != 42)
			TEST_FAILED("Expected num=42");

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseEmptyObject()
	{
		// Empty object
		Core::ObjectDescription emptyObj = Core::LoadObjectDescriptionFromJsonString("{}");
		if (!emptyObj.IsObject())
			TEST_FAILED("Expected object type for empty object");
		if (emptyObj.GetAsDescriptionMap().size() != 0)
			TEST_FAILED("Empty object should have 0 entries");

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestParseNestedStructure()
	{
		Core::ObjectDescription nested = Core::LoadObjectDescriptionFromJsonString(
			R"({"outer": {"inner": [1, 2, {"deep": true}]}})"
		);

		if (!nested.IsObject())
			TEST_FAILED("Expected object type at root");

		const auto& outerMap = nested["outer"].GetAsDescriptionMap();
		if (outerMap.size() != 1)
			TEST_FAILED("Expected 1 entry in outer object");

		const auto& innerArr = nested["outer"]["inner"].GetAsObjectArray();
		if (innerArr.Count() != 3)
		{
			Core::String msg = Core::FormatString("Expected 3 elements in inner array, got {0}", innerArr.Count());
			TEST_FAILED(msg);
		}

		if (!innerArr[0].IsUint() || innerArr[0].GetAsUint() != 1)
			TEST_FAILED("First inner element should be 1");
		if (!innerArr[1].IsUint() || innerArr[1].GetAsUint() != 2)
			TEST_FAILED("Second inner element should be 2");

		const auto& deepMap = innerArr[2].GetAsDescriptionMap();
		if (deepMap.size() != 1)
			TEST_FAILED("Expected 1 entry in deep object");
		if (!innerArr[2]["deep"].IsBool() || !innerArr[2]["deep"].GetAsBool())
			TEST_FAILED("Deep value should be true");

		TEST_PASSED();
	}

	void JsonDescriptionTest::TestLoadFromFile()
	{
		Core::ObjectDescription desc = Core::LoadObjectDescriptionFromJsonFile(
			fs::path("assets/test.json")
		);

		if (!desc.IsObject())
			TEST_FAILED("Expected object type from file, got type {0}", static_cast<int>(desc.GetType()));

		// Verify top-level fields
		if (!desc.HasEntery("name"))
			TEST_FAILED("Missing 'name' entry in loaded JSON");
		if (desc["name"].GetAsString() != "test")
		{
			Core::String msg = Core::FormatString("Expected name='test', got '{0}'", desc["name"].GetAsString());
			TEST_FAILED(msg);
		}

		if (!desc.HasEntery("value"))
			TEST_FAILED("Missing 'value' entry in loaded JSON");
		if (desc["value"].GetAsUint() != 42)
		{
			Core::String msg = Core::FormatString("Expected value=42, got {0}", desc["value"].GetAsUint());
			TEST_FAILED(msg);
		}

		if (!desc.HasEntery("enabled"))
			TEST_FAILED("Missing 'enabled' entry in loaded JSON");
		if (!desc["enabled"].GetAsBool())
			TEST_FAILED("Expected enabled=true");

		// Verify nested object
		if (!desc.HasEntery("nested"))
			TEST_FAILED("Missing 'nested' entry in loaded JSON");
		const auto& nested = desc["nested"].GetAsDescriptionMap();
		if (std::abs(nested.at("innerValue").GetAsFloat() - 3.14) > 0.001)
		{
			Core::String msg = Core::FormatString("Expected innerValue=3.14, got {0}", nested.at("innerValue").GetAsFloat());
			TEST_FAILED(msg);
		}

		const auto& innerArray = nested.at("innerArray").GetAsObjectArray();
		if (innerArray.Count() != 3)
		{
			Core::String msg = Core::FormatString("Expected innerArray with 3 elements, got {0}", innerArray.Count());
			TEST_FAILED(msg);
		}

		// Verify array of objects
		const auto& arrayOfObjects = desc["arrayOfObjects"].GetAsObjectArray();
		if (arrayOfObjects.Count() != 2)
		{
			Core::String msg = Core::FormatString("Expected arrayOfObjects with 2 elements, got {0}", arrayOfObjects.Count());
			TEST_FAILED(msg);
		}

		// Verify empty containers
		if (!desc["emptyObject"].IsObject() || desc["emptyObject"].GetAsDescriptionMap().size() != 0)
			TEST_FAILED("Expected empty object");
		if (!desc["emptyArray"].IsArray() || desc["emptyArray"].GetAsObjectArray().Count() != 0)
			TEST_FAILED("Expected empty array");

		TEST_PASSED();
	}

}
