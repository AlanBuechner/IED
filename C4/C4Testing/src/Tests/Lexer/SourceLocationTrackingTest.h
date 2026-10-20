#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"

namespace Testing
{
	CLASS(GROUP = Testing) SourceLocationTrackingTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SourceLocationTrackingTest);

		// --- Basic single token ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestFirstTokenStartLocation();

		TEST_FUNC(Lexer.SourceLocation)
		void TestFirstTokenEndLocation();

		TEST_FUNC(Lexer.SourceLocation)
		void TestSingleCharToken();

		// --- Leading whitespace before first token ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestLeadingSpacesBeforeFirstToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestLeadingTabsBeforeFirstToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestLeadingNewlinesBeforeFirstToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestLeadingMixedWhitespaceBeforeFirstToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestLeadingCRLFBeforeFirstToken();

		// --- Multi-token position tracking ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestSecondTokenStartAfterFirst();

		TEST_FUNC(Lexer.SourceLocation)
		void TestLocationAcrossWhitespace();

		TEST_FUNC(Lexer.SourceLocation)
		void TestMultipleSpacesBetweenTokens();

		// --- Newline handling ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestLineIncrementOnNewline();

		TEST_FUNC(Lexer.SourceLocation)
		void TestColumnResetOnNewline();

		TEST_FUNC(Lexer.SourceLocation)
		void TestMultiLineTokenLocations();

		TEST_FUNC(Lexer.SourceLocation)
		void TestWhitespaceBeforeAndAfterNewline();

		TEST_FUNC(Lexer.SourceLocation)
		void TestMultipleBlankLinesBetweenTokens();

		TEST_FUNC(Lexer.SourceLocation)
		void TestCRLFLineEndings();

		// --- Byte offset tracking ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestByteOffsetAccumulation();

		TEST_FUNC(Lexer.SourceLocation)
		void TestStartEndSpanMatchesTokenLength();

		// --- End location correctness ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestEndLocationMultiCharToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestEndLocationAfterNewlineInToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestEndLocationCompoundOperator();

		TEST_FUNC(Lexer.SourceLocation)
		void TestEndLocationDoubleColon();

		// --- Token type-specific locations ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestStringLiteralLocation();

		TEST_FUNC(Lexer.SourceLocation)
		void TestNumberTokenLocation();

		TEST_FUNC(Lexer.SourceLocation)
		void TestKeywordTokenLocation();

		TEST_FUNC(Lexer.SourceLocation)
		void TestLongIdentifierEndLocation();

		// --- Comment skipping ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestTokenAfterComment();

		TEST_FUNC(Lexer.SourceLocation)
		void TestTokenAfterMultiLineComment();

		TEST_FUNC(Lexer.SourceLocation)
		void TestTokenAfterMultipleComments();

		// --- Edge cases ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestTokenAtExactEOF();

		TEST_FUNC(Lexer.SourceLocation)
		void TestTabCharacterWhitespace();

		TEST_FUNC(Lexer.SourceLocation)
		void TestMixedWhitespaceBeforeToken();

		TEST_FUNC(Lexer.SourceLocation)
		void TestOnlyWhitespaceProducesEOF();

		TEST_FUNC(Lexer.SourceLocation)
		void TestSourceIdInLocation();

		// --- Span validation ---
		TEST_FUNC(Lexer.SourceLocation)
		void TestSpanEqualsTextLengthForAllTokenTypes();
	};
}
