#include "SourceLocationTrackingTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	// --- Basic single token ---

	void SourceLocationTrackingTest::TestFirstTokenStartLocation()
	{
		auto source = Scripting::Source::FromMemory("hello");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 0);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestFirstTokenEndLocation()
	{
		// Token "hello" has length 5, starts at column 1 offset 0,
		// end location should be column 6 (one past last char), offset 5.
		auto source = Scripting::Source::FromMemory("hello");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(end.line == 1);
		TEST_ASSERT(end.column == 6);
		TEST_ASSERT(end.byteOffset == 5);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestSingleCharToken()
	{
		auto source = Scripting::Source::FromMemory("=");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 0);
		TEST_ASSERT(end.line == 1);
		TEST_ASSERT(end.column == 2);
		TEST_ASSERT(end.byteOffset == 1);
		TEST_PASSED();
	}

	// --- Leading whitespace before first token ---

	void SourceLocationTrackingTest::TestLeadingSpacesBeforeFirstToken()
	{
		// "   hello": three spaces then "hello"
		// ' '(0) ' '(1) ' '(2) 'h'(3) ...
		auto source = Scripting::Source::FromMemory("   hello");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 4);  // after three spaces
		TEST_ASSERT(start.byteOffset == 3);
		TEST_ASSERT(token.GetText() == "hello");
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestLeadingTabsBeforeFirstToken()
	{
		// "\t\tfoo": two tabs then "foo"
		// '\t'(0) '\t'(1) 'f'(2) ...
		auto source = Scripting::Source::FromMemory("\t\tfoo");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 3);  // after two tabs
		TEST_ASSERT(start.byteOffset == 2);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestLeadingNewlinesBeforeFirstToken()
	{
		// "\n\nbar": two newlines then "bar"
		// '\n'(0) '\n'(1) 'b'(2) ...
		auto source = Scripting::Source::FromMemory("\n\nbar");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 3);    // after two newlines
		TEST_ASSERT(start.column == 1);  // first column of line 3
		TEST_ASSERT(start.byteOffset == 2);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestLeadingMixedWhitespaceBeforeFirstToken()
	{
		// " \t\n baz": space, tab, newline, space then "baz"
		// ' '(0) '\t'(1) '\n'(2) ' '(3) 'b'(4) ...
		auto source = Scripting::Source::FromMemory(" \t\n baz");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);    // after the newline
		TEST_ASSERT(start.column == 2);  // one space on line 2, then 'b'
		TEST_ASSERT(start.byteOffset == 4);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestLeadingCRLFBeforeFirstToken()
	{
		// "\r\n\r\ntest": two CRLF sequences then "test"
		// '\r'(0) '\n'(1) '\r'(2) '\n'(3) 't'(4) ...
		auto source = Scripting::Source::FromMemory("\r\n\r\ntest");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 3);    // after two CRLF sequences
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 4);
		TEST_PASSED();
	}

	// --- Multi-token position tracking ---

	void SourceLocationTrackingTest::TestSecondTokenStartAfterFirst()
	{
		// "ab cd": 'a'(0) 'b'(1) ' '(2) 'c'(3) 'd'(4)
		// First token "ab" at offsets 0-1. Space consumed as separator.
		// Second token "cd" starts at line=1, column=4, byteOffset=3.
		auto source = Scripting::Source::FromMemory("ab cd");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "ab"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 4);
		TEST_ASSERT(start.byteOffset == 3);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestLocationAcrossWhitespace()
	{
		// "a  b": 'a'(0) ' '(1) ' '(2) 'b'(3)
		// Two spaces between tokens. Token 'b' starts at byteOffset=3, column=4.
		auto source = Scripting::Source::FromMemory("a  b");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 4);
		TEST_ASSERT(start.byteOffset == 3);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestMultipleSpacesBetweenTokens()
	{
		// "a   b": 'a'(0) ' '(1) ' '(2) ' '(3) 'b'(4)
		// Three spaces. Token 'b' starts at byteOffset=4, column=5.
		auto source = Scripting::Source::FromMemory("a   b");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 5);
		TEST_ASSERT(start.byteOffset == 4);
		TEST_PASSED();
	}

	// --- Newline handling ---

	void SourceLocationTrackingTest::TestLineIncrementOnNewline()
	{
		// "a\nb": newline separates tokens, second token should be on line 2.
		auto source = Scripting::Source::FromMemory("a\nb");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);
		TEST_ASSERT(start.column == 1);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestColumnResetOnNewline()
	{
		// "x\ny": after newline, column resets to 0 so next token starts at column 1.
		auto source = Scripting::Source::FromMemory("x\ny");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "x" on line 1
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);
		TEST_ASSERT(start.column == 1);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestMultiLineTokenLocations()
	{
		// "a\nb\nc": three tokens on separate lines.
		// 'a'(0) '\n'(1) 'b'(2) '\n'(3) 'c'(4)
		auto source = Scripting::Source::FromMemory("a\nb\nc");
		Scripting::Lexer lexer(*source, 0);

		auto t0 = lexer.NextCompoundToken();
		TEST_ASSERT(t0.GetStartLocation().line == 1);
		TEST_ASSERT(t0.GetStartLocation().byteOffset == 0);

		auto t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetStartLocation().line == 2);
		TEST_ASSERT(t1.GetStartLocation().byteOffset == 2); // 'a'(0) + '\n'(1), 'b' at offset 2

		auto t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetStartLocation().line == 3);
		TEST_ASSERT(t2.GetStartLocation().byteOffset == 4); // + 'b'(2) + '\n'(3), 'c' at offset 4
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestWhitespaceBeforeAndAfterNewline()
	{
		// "a\n  b": newline followed by two spaces before token.
		// 'a'(0) '\n'(1) ' '(2) ' '(3) 'b'(4)
		// Token 'b' should be on line 2, column=3 (after two spaces), byteOffset=4.
		auto source = Scripting::Source::FromMemory("a\n  b");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);
		TEST_ASSERT(start.column == 3);
		TEST_ASSERT(start.byteOffset == 4);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestMultipleBlankLinesBetweenTokens()
	{
		// "a\n\n\nb": 'a'(0) '\n'(1) '\n'(2) '\n'(3) 'b'(4)
		// Three newlines after 'a'. Token 'b' is on line 4, at byteOffset 4.
		auto source = Scripting::Source::FromMemory("a\n\n\nb");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a" (line 1)
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 4);    // three newlines -> line 4
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 4); // 'b' at offset 4 (after a + 3 newlines)
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestCRLFLineEndings()
	{
		// "a\r\nb": CRLF between tokens.
		// 'a'(0) '\r'(1) '\n'(2) 'b'(3)
		auto source = Scripting::Source::FromMemory("a\r\nb");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 3); // 'b' at offset 3 (after CR and LF)
		TEST_PASSED();
	}

	// --- Byte offset tracking ---

	void SourceLocationTrackingTest::TestByteOffsetAccumulation()
	{
		// "ab cd ef": 'a'(0)'b'(1)' '(2)'c'(3)'d'(4)' '(5)'e'(6)'f'(7)
		auto source = Scripting::Source::FromMemory("ab cd ef");
		Scripting::Lexer lexer(*source, 0);

		auto t0 = lexer.NextCompoundToken();
		TEST_ASSERT(t0.GetStartLocation().byteOffset == 0);

		auto t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetStartLocation().byteOffset == 3); // 'c' at offset 3

		auto t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetStartLocation().byteOffset == 6); // 'e' at offset 6
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestStartEndSpanMatchesTokenLength()
	{
		// Token "world" in "hello world": end.byteOffset - start.byteOffset should equal token text length.
		auto source = Scripting::Source::FromMemory("hello world");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "hello"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();

		size_t span = end.byteOffset - start.byteOffset;
		TEST_ASSERT(span == token.GetText().size());
		TEST_PASSED();
	}

	// --- End location correctness ---

	void SourceLocationTrackingTest::TestEndLocationMultiCharToken()
	{
		// "abc def": first token "abc", end should be one past 'c'.
		// 'a'(0)'b'(1)'c'(2)' '(3)...
		auto source = Scripting::Source::FromMemory("abc def");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(end.line == 1);
		TEST_ASSERT(end.column == 4); // one past 'c' (column 3)
		TEST_ASSERT(end.byteOffset == 3);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestEndLocationAfterNewlineInToken()
	{
		// Multi-line comment spanning lines. "/* a\nb */"
		// '/'(0)'*'(1)' '(2)'a'(3)'\n'(4)'b'(5)' '(6)'*'(7)'/'(8)
		auto source = Scripting::Source::FromMemory("/* a\nb */");
		Scripting::Lexer lexer(*source, 0);

		// Use NextToken() because NextCompoundToken() skips comments.
		auto token = lexer.NextToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.byteOffset == 0);
		// End location should be on line 2 (after '\n') or at the closing '/'
		// The closing '/' is at offset 8, which is on line 2
		TEST_ASSERT(end.line == 2);
		TEST_ASSERT(end.byteOffset == 9); // one past closing '/'
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestEndLocationCompoundOperator()
	{
		// "==": two-character operator. '='(0) '='(1)
		auto source = Scripting::Source::FromMemory("==");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.byteOffset == 0);
		TEST_ASSERT(end.byteOffset == 2); // one past second '='
		TEST_ASSERT(end.column == 3);
		size_t span = end.byteOffset - start.byteOffset;
		TEST_ASSERT(span == token.GetText().size());
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestEndLocationDoubleColon()
	{
		// "::": two-character operator. ':'(0) ':'(1)
		auto source = Scripting::Source::FromMemory("::");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.byteOffset == 0);
		TEST_ASSERT(end.byteOffset == 2);
		size_t span = end.byteOffset - start.byteOffset;
		TEST_ASSERT(span == token.GetText().size());
		TEST_PASSED();
	}

	// --- Token type-specific locations ---

	void SourceLocationTrackingTest::TestStringLiteralLocation()
	{
		// "\"hi\"": string literal. '"'(0) 'h'(1) 'i'(2) '"'(3)
		auto source = Scripting::Source::FromMemory("\"hi\"");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 0);
		// End is one past the closing quote
		TEST_ASSERT(end.byteOffset == 4);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestNumberTokenLocation()
	{
		// " 42": two spaces before number. ' '(0) ' '(1) '4'(2) '2'(3)
		auto source = Scripting::Source::FromMemory("  42");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 3); // after two spaces, '4' at column 3
		TEST_ASSERT(start.byteOffset == 2);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestKeywordTokenLocation()
	{
		// "   return": three spaces before keyword. ' '(0)' '(1)' '(2)'r'(3)...
		auto source = Scripting::Source::FromMemory("   return");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 4);
		TEST_ASSERT(start.byteOffset == 3);
		TEST_ASSERT(token.GetType() == Scripting::TokenType::Return);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestLongIdentifierEndLocation()
	{
		// "myVeryLongVariableName" = 20 chars
		auto source = Scripting::Source::FromMemory("myVeryLongVariableName");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.byteOffset == 0);
		TEST_ASSERT(end.byteOffset == 22); // "myVeryLongVariableName" is 22 chars
		size_t span = end.byteOffset - start.byteOffset;
		TEST_ASSERT(span == token.GetText().size());
		TEST_PASSED();
	}

	// --- Comment skipping ---

	void SourceLocationTrackingTest::TestTokenAfterComment()
	{
		// "// comment\nhello": comment on line 1, token on line 2.
		// '/'(0)'/'(1)' '(2)'c'(3)'o'(4)'m'(5)'m'(6)'e'(7)'n'(8)'t'(9)'\n'(10)'h'(11)...
		auto source = Scripting::Source::FromMemory("// comment\nhello");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken(); // comment is skipped, returns "hello"
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 11); // "// comment\n" = 11 bytes (indices 0-10)
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestTokenAfterMultiLineComment()
	{
		// "/* block */x": multi-line comment then token 'x'
		// '/'(0)'*'(1)' '(2)'b'(3)'l'(4)'o'(5)'c'(6)'k'(7)' '(8)'*'(9)'/'(10)'x'(11)
		auto source = Scripting::Source::FromMemory("/* block */x");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken(); // comment skipped, returns "x"
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.column == 12); // after "/* block */" (11 chars)
		TEST_ASSERT(start.byteOffset == 11);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestTokenAfterMultipleComments()
	{
		// "// a\n// b\nc": two comments then token 'c'
		// '/'(0)'/'(1)' '(2)'a'(3)'\n'(4)'/'(5)'/'(6)' '(7)'b'(8)'\n'(9)'c'(10)
		auto source = Scripting::Source::FromMemory("// a\n// b\nc");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken(); // both comments skipped, returns "c"
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 3);
		TEST_ASSERT(start.column == 1);
		TEST_ASSERT(start.byteOffset == 10);
		TEST_PASSED();
	}

	// --- Edge cases ---

	void SourceLocationTrackingTest::TestTokenAtExactEOF()
	{
		// Single token at end of stream with no trailing whitespace.
		auto source = Scripting::Source::FromMemory("xyz");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.byteOffset == 0);
		TEST_ASSERT(end.byteOffset == 3);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestTabCharacterWhitespace()
	{
		// "a\tb": tab separates tokens. 'a'(0) '\t'(1) 'b'(2)
		auto source = Scripting::Source::FromMemory("a\tb");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 1);
		TEST_ASSERT(start.byteOffset == 2); // 'b' at offset 2
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestMixedWhitespaceBeforeToken()
	{
		// "a \t\n b": mixed whitespace (space, tab, newline) before 'b'.
		// 'a'(0) ' '(1) '\t'(2) '\n'(3) ' '(4) 'b'(5)
		auto source = Scripting::Source::FromMemory("a \t\n b");
		Scripting::Lexer lexer(*source, 0);

		lexer.NextCompoundToken(); // consume "a"
		auto token = lexer.Peek();
		const auto& start = token.GetStartLocation();
		TEST_ASSERT(start.line == 2);   // after the newline
		TEST_ASSERT(start.column == 2); // one space on line 2, then 'b' at col 2
		TEST_ASSERT(start.byteOffset == 5); // 'b' at offset 5
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestOnlyWhitespaceProducesEOF()
	{
		// "   \n\t": only whitespace, should produce EOF token.
		auto source = Scripting::Source::FromMemory("   \n\t");
		Scripting::Lexer lexer(*source, 0);

		auto token = lexer.NextCompoundToken();
		TEST_ASSERT(token.GetType() == Scripting::TokenType::EndOfFile);
		const auto& loc = token.GetStartLocation();
		// After consuming: 3 spaces on line 1, then newline -> line 2, tab on line 2.
		// So position should be line 2, column 2 (tab advances column), byteOffset 5.
		TEST_ASSERT(loc.line == 2);
		TEST_ASSERT(loc.column == 2);
		TEST_ASSERT(loc.byteOffset == 5);
		TEST_PASSED();
	}

	void SourceLocationTrackingTest::TestSourceIdInLocation()
	{
		auto source = Scripting::Source::FromMemory("hello");
		Scripting::Lexer lexer(*source, 42); // source ID = 42

		auto token = lexer.NextCompoundToken();
		const auto& start = token.GetStartLocation();
		const auto& end = token.GetEndLocation();
		TEST_ASSERT(start.sourceId == 42);
		TEST_ASSERT(end.sourceId == 42);
		TEST_PASSED();
	}

	// --- Span validation across all token types ---

	void SourceLocationTrackingTest::TestSpanEqualsTextLengthForAllTokenTypes()
	{
		auto source = Scripting::Source::FromMemory("x + y == z;");
		Scripting::Lexer lexer(*source, 0);

		// "x" - identifier (1 char)
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::Identifier);
			size_t span = token.GetEndLocation().byteOffset - token.GetStartLocation().byteOffset;
			TEST_ASSERT(span == token.GetText().size());
		}

		// "+" - single char operator (1 char)
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::Plus);
			size_t span = token.GetEndLocation().byteOffset - token.GetStartLocation().byteOffset;
			TEST_ASSERT(span == token.GetText().size());
		}

		// "y" - identifier (1 char)
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::Identifier);
			size_t span = token.GetEndLocation().byteOffset - token.GetStartLocation().byteOffset;
			TEST_ASSERT(span == token.GetText().size());
		}

		// "==" - double char operator (2 chars)
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::DoubleEqual);
			size_t span = token.GetEndLocation().byteOffset - token.GetStartLocation().byteOffset;
			TEST_ASSERT(span == token.GetText().size());
		}

		// "z" - identifier (1 char)
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::Identifier);
			size_t span = token.GetEndLocation().byteOffset - token.GetStartLocation().byteOffset;
			TEST_ASSERT(span == token.GetText().size());
		}

		// ";" - single char operator (1 char)
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::SemiColon);
			size_t span = token.GetEndLocation().byteOffset - token.GetStartLocation().byteOffset;
			TEST_ASSERT(span == token.GetText().size());
		}

		// EOF
		{
			auto token = lexer.NextCompoundToken();
			TEST_ASSERT(token.GetType() == Scripting::TokenType::EndOfFile);
		}

		TEST_PASSED();
	}
}
