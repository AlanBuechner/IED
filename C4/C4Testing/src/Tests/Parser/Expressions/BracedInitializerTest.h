#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A braced list as the expression parser builds and prints it: entries in source order, as written.
	CLASS(GROUP = Testing) BracedInitializerTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BracedInitializerTest);

		TEST_FUNC(Parser.Pratt.Braces)
		void PrintsPositionalEntries();
		TEST_FUNC(Parser.Pratt.Braces)
		void PrintsDesignators();
		TEST_FUNC(Parser.Pratt.Braces)
		void PrintsAnEmptyList();
		TEST_FUNC(Parser.Pratt.Braces)
		void KeepsBracketedAndFieldKeysApart();
	};
} // namespace Testing
