#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Macro expansion (B4, BUG-125, BUG-127): parameters substitute at once and never inside literals,
	// arguments expand first except as operands of `#` and `##`, the result is rescanned without the macro
	// itself, `__FILE__` / `__LINE__`, variadic macros and argument-count errors.
	CLASS(GROUP = Testing) MacroExpanderTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MacroExpanderTest);

		TEST_FUNC(Macro.Expander.Substitution)
		void ParametersSubstituteAtOnce();
		TEST_FUNC(Macro.Expander.Substitution)
		void LiteralsAndCommentsAreKept();
		TEST_FUNC(Macro.Expander.Operators)
		void StringizeQuotesTheArgumentAsWritten();
		TEST_FUNC(Macro.Expander.Operators)
		void PasteJoinsTokens();
		TEST_FUNC(Macro.Expander.Rescan)
		void ResultsAreRescannedWithoutRecursion();
		TEST_FUNC(Macro.Expander.Arguments)
		void ArgumentCountsAndVariadics();
		TEST_FUNC(Macro.Expander.Builtins)
		void FileAndLine();
		TEST_FUNC(Macro.Expander.Calls)
		void CallsThatContinueOnTheNextLine();
	};
} // namespace Testing
