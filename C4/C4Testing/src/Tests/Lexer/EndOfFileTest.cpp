#include "EndOfFileTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	void EndOfFileTest::TestEOFTokenAfterEmptyStream()
	{
		auto stream = Scripting::Source::FromMemory("");
		Scripting::Lexer lexer(*stream, 0);

		auto token = lexer.Peek();
		TEST_ASSERT(token.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void EndOfFileTest::TestEOFTokenAfterSingleToken()
	{
		auto stream = Scripting::Source::FromMemory("hello");
		Scripting::Lexer lexer(*stream, 0);

		lexer.NextCompoundToken(); // skip "hello" (safe: Peek + NextToken)
		auto token = lexer.Peek();
		TEST_ASSERT(token.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void EndOfFileTest::TestEOFTokenTypeIsEndOfFile()
	{
		auto stream = Scripting::Source::FromMemory("x");
		Scripting::Lexer lexer(*stream, 0);

		auto ident = lexer.NextCompoundToken();
		TEST_ASSERT(ident.GetType() == Scripting::TokenType::Identifier);

		auto eof = lexer.NextCompoundToken();
		TEST_ASSERT(eof.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void EndOfFileTest::TestEofLocationAfterContent()
	{
		// "abc" has 3 bytes, EOF should be at line=1, column=4, byteOffset=3.
		auto stream = Scripting::Source::FromMemory("abc");
		Scripting::Lexer lexer(*stream, 0);

		lexer.NextCompoundToken(); // skip "abc"
		auto eof = lexer.Peek();
		const auto& loc = eof.GetStartLocation();
		TEST_ASSERT(loc.line == 1);
		TEST_ASSERT(loc.column == 4);
		TEST_ASSERT(loc.byteOffset == 3);
		TEST_PASSED();
	}

	void EndOfFileTest::TestEofStartEqualsEndLocation()
	{
		// EOF token has zero length, so start and end locations should be equal.
		auto stream = Scripting::Source::FromMemory("test");
		Scripting::Lexer lexer(*stream, 0);

		lexer.NextCompoundToken(); // skip "test"
		auto eof = lexer.Peek();
		const auto& start = eof.GetStartLocation();
		const auto& end = eof.GetEndLocation();
		TEST_ASSERT(start.line == end.line);
		TEST_ASSERT(start.column == end.column);
		TEST_ASSERT(start.byteOffset == end.byteOffset);
		TEST_PASSED();
	}

	void EndOfFileTest::TestGetNextTokenReturnsEOFAtEnd()
	{
		auto stream = Scripting::Source::FromMemory("1 2");
		Scripting::Lexer lexer(*stream, 0);

		auto t0 = lexer.NextCompoundToken();
		TEST_ASSERT(t0.GetType() == Scripting::TokenType::Number);

		auto t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetType() == Scripting::TokenType::Number);

		auto eof = lexer.NextCompoundToken();
		TEST_ASSERT(eof.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void EndOfFileTest::TestMultiplePeekAfterEOF()
	{
		// After reaching EOF, additional Peek calls should still return EOF.
		auto stream = Scripting::Source::FromMemory("a");
		Scripting::Lexer lexer(*stream, 0);

		lexer.NextCompoundToken(); // skip "a"

		auto eof1 = lexer.Peek();
		TEST_ASSERT(eof1.GetType() == Scripting::TokenType::EndOfFile);

		// Peek again - EOF token is cached in the deque, should still be available.
		auto eof2 = lexer.Peek();
		TEST_ASSERT(eof2.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}
}
