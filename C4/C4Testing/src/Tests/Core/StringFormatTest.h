#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	CLASS(GROUP = Testing) StringFormatTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StringFormatTest);

		// {0}, {1} and repeated placeholders substitute correctly.
		TEST_FUNC(Core.StringFormat)
		void TestBasicPlaceholders();

		// A brace sequence that is not a valid placeholder must render as literal text, never
		// discard the message (ErrorTracker drops empty diagnostics, so a swallowed message is
		// a silently lost error).
		TEST_FUNC(Core.StringFormat)
		void TestInvalidBracesAreLiteral();

		// "\{" renders a literal brace that can never be parsed as a placeholder.
		TEST_FUNC(Core.StringFormat)
		void TestEscapedBrace();

		// Unterminated brace: the rest of the message is kept verbatim.
		TEST_FUNC(Core.StringFormat)
		void TestUnterminatedBraceKeepsRest();

		// An out-of-range index renders a visible marker, not silence.
		TEST_FUNC(Core.StringFormat)
		void TestOutOfRangeIndexIsVisible();
	};
} // namespace Testing
