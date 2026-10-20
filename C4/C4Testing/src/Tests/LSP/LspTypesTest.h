#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Protocol/LspTypes.h"
#include "Lexer/Token.h" // Scripting::SourceLocation

namespace Testing
{
	// Exercises the 1-based compiler SourceLocation <-> 0-based LSP Position conversion and the
	// file:// URI helpers that map editor URIs to filesystem paths.
	CLASS(GROUP = Testing) LspTypesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspTypesTest);

		TEST_FUNC(Lsp.Types)
		void TestPositionConvertsOneBasedToZeroBased();

		TEST_FUNC(Lsp.Types)
		void TestRangeFromSourceRange();

		TEST_FUNC(Lsp.Types)
		void TestUriToUriForWindowsPath();

		TEST_FUNC(Lsp.Types)
		void TestUriRoundTripNormalizesSeparators();

		TEST_FUNC(Lsp.Types)
		void TestLocationMakeUsesConvertedRange();
	};
} // namespace Testing
