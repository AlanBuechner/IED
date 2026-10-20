#include "StringFormatTest.h"
#include "Core/StringFormat.h"

namespace Testing
{
	void StringFormatTest::TestBasicPlaceholders()
	{
		TEST_ASSERT_EQ(Core::FormatString("Hello {0}!", std::string("world")), std::string("Hello world!"));
		TEST_ASSERT_EQ(Core::FormatString("{0}+{1}={2}", 1, 2, 3), std::string("1+2=3"));
		// A placeholder may repeat.
		TEST_ASSERT_EQ(Core::FormatString("{0}{0}", 7), std::string("77"));
		// No placeholders: the message is returned verbatim.
		TEST_ASSERT_EQ(Core::FormatString("plain text"), std::string("plain text"));
		// Multi-digit index.
		std::string a = "x"; std::string b = "y"; int z = 9;
		TEST_ASSERT_EQ(Core::FormatString("{0}..{1}..{2}", a, b, z), std::string("x..y..9"));
		TEST_PASSED();
	}

	void StringFormatTest::TestInvalidBracesAreLiteral()
	{
		// Non-numeric content between braces is literal, NOT a discarded message.
		TEST_ASSERT_EQ(Core::FormatString("expected ';' got '{'"), std::string("expected ';' got '{'"));
		// Empty braces are literal.
		TEST_ASSERT_EQ(Core::FormatString("value {} here"), std::string("value {} here"));
		// A literal brace must not swallow a real placeholder that follows it.
		TEST_ASSERT_EQ(Core::FormatString("got {x} then {0}", 5), std::string("got {x} then 5"));
		TEST_PASSED();
	}

	void StringFormatTest::TestEscapedBrace()
	{
		// "\{" renders a literal brace that is never parsed as a placeholder.
		TEST_ASSERT_EQ(Core::FormatString("a \\{ b"), std::string("a { b"));
		// An escaped brace followed by a real placeholder keeps both correct.
		TEST_ASSERT_EQ(Core::FormatString("\\{0} and {0}", 3), std::string("{0} and 3"));
		TEST_PASSED();
	}

	void StringFormatTest::TestUnterminatedBraceKeepsRest()
	{
		// An unterminated '{' keeps the rest of the message verbatim.
		TEST_ASSERT_EQ(Core::FormatString("oops {"), std::string("oops {"));
		TEST_PASSED();
	}

	void StringFormatTest::TestOutOfRangeIndexIsVisible()
	{
		// An index beyond the argument count renders a visible marker, not silence.
		std::string out = Core::FormatString("only one arg but {5}", 1);
		TEST_ASSERT(out.find("invalid param index") != std::string::npos);
		TEST_PASSED();
	}
} // namespace Testing
