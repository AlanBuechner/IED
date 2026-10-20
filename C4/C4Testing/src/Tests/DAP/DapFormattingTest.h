#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The debug adapter's honest pieces that turn bytes and names into text: scalars, addresses, string literals,
	// thrown types' names, UTF-8 boundaries in the program's output, and a launched program's command line.
	CLASS(GROUP = Testing) DapFormattingTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapFormattingTest);

		TEST_FUNC(Dap.Formatting)
		void IntegersAreDecimalAndSignExtended();
		TEST_FUNC(Dap.Formatting)
		void FloatsAreShortestExact();
		TEST_FUNC(Dap.Formatting)
		void CharactersShowTheirCodeAndGlyph();
		TEST_FUNC(Dap.Formatting)
		void BasicTypesUseC4Names();
		TEST_FUNC(Dap.Formatting)
		void StringsAreQuotedAndEscaped();
		TEST_FUNC(Dap.Formatting)
		void TypeDescriptorNamesAreScoped();
		TEST_FUNC(Dap.Formatting)
		void OutputIsCutOnlyBetweenCharacters();
		TEST_FUNC(Dap.Formatting)
		void CommandLinesSurviveTheRuntimesSplit();
	};
} // namespace Testing
