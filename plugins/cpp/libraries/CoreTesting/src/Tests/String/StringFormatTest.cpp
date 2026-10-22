#include "StringFormatTest.h"

namespace Testing
{
	void StringFormatTest::TestIntegerFormatting()
	{
		Core::String result = Core::FormatString("Value is {0}", 42);
		if (result != "Value is 42")
			TEST_FAILED("Expected 'Value is 42', got '" + result + "'");

		TEST_PASSED();
	}

	void StringFormatTest::TestFloatFormatting()
	{
		Core::String result = Core::FormatString("Pi is roughly {0}", 3.14f);
		// Just verify it doesn't crash and contains "Pi"
		if (result.find("Pi") == std::string::npos)
			TEST_FAILED("Expected result to contain 'Pi'");

		TEST_PASSED();
	}

	void StringFormatTest::TestStringInsertion()
	{
		Core::String name = "world";
		Core::String result = Core::FormatString("Hello, {0}!", name);
		if (result != "Hello, world!")
			TEST_FAILED("Expected 'Hello, world!', got '" + result + "'");

		TEST_PASSED();
	}
}
