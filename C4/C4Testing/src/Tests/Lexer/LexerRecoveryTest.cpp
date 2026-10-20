#include "LexerRecoveryTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	// =====================================================================
	// Predicate-based Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverUntilFindsToken()
	{
		auto stream = Scripting::Source::FromMemory("x y 42 z");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverUntil(lexer, [](const Scripting::Token& t) -> bool
		{
			return t.GetType() == Scripting::TokenType::Number;
		});

		TEST_ASSERT(result.GetType() == Scripting::TokenType::Number);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilStopsOnFirstMatch()
	{
		auto stream = Scripting::Source::FromMemory("42 x y");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverUntil(lexer, [](const Scripting::Token& t) -> bool
		{
			return t.GetType() == Scripting::TokenType::Number;
		});

		TEST_ASSERT(result.GetType() == Scripting::TokenType::Number);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilReturnsEOFWhenNotFound()
	{
		auto stream = Scripting::Source::FromMemory("x y z");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverUntil(lexer, [](const Scripting::Token& t) -> bool
		{
			return t.GetType() == Scripting::TokenType::Number;
		});

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilEmptyInput()
	{
		auto stream = Scripting::Source::FromMemory("");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverUntil(lexer, [](const Scripting::Token& t) -> bool
		{
			return false;
		});

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilCompoundTokens()
	{
		auto stream = Scripting::Source::FromMemory("x++ y==z");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverUntil(lexer, [](const Scripting::Token& t) -> bool
		{
			return t.GetType() == Scripting::TokenType::DoubleEqual;
		});

		TEST_ASSERT(result.GetType() == Scripting::TokenType::DoubleEqual);
		TEST_PASSED();
	}

	// =====================================================================
	// Token-Type Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToTokenTypeFindsTarget()
	{
		auto stream = Scripting::Source::FromMemory("a b c { d");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToTokenType(lexer, Scripting::TokenType::OpenCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::OpenCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToTokenTypeAlreadyAtTarget()
	{
		auto stream = Scripting::Source::FromMemory("{ a b");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToTokenType(lexer, Scripting::TokenType::OpenCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::OpenCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToTokenTypeReturnsEOFWhenNotFound()
	{
		auto stream = Scripting::Source::FromMemory("a b c");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToTokenType(lexer, Scripting::TokenType::OpenCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	// =====================================================================
	// Any-Token-Type Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeFindsFirstMatch()
	{
		auto stream = Scripting::Source::FromMemory("x y + z");
		Scripting::Lexer lexer(*stream, 0);

		Core::Vector<Scripting::TokenType> targets;
		targets.Push(Scripting::TokenType::Minus);
		targets.Push(Scripting::TokenType::Plus);

		const auto& result = Scripting::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::Plus);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeMultipleTargets()
	{
		auto stream = Scripting::Source::FromMemory("a b c { }");
		Scripting::Lexer lexer(*stream, 0);

		Core::Vector<Scripting::TokenType> targets;
		targets.Push(Scripting::TokenType::CloseCurl);
		targets.Push(Scripting::TokenType::OpenCurl);

		const auto& result = Scripting::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::OpenCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeReturnsEOFWhenNotFound()
	{
		auto stream = Scripting::Source::FromMemory("a b c");
		Scripting::Lexer lexer(*stream, 0);

		Core::Vector<Scripting::TokenType> targets;
		targets.Push(Scripting::TokenType::OpenCurl);
		targets.Push(Scripting::TokenType::CloseCurl);

		const auto& result = Scripting::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeEmptyList()
	{
		auto stream = Scripting::Source::FromMemory("a b c");
		Scripting::Lexer lexer(*stream, 0);

		Core::Vector<Scripting::TokenType> targets;

		const auto& result = Scripting::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	// =====================================================================
	// Scope Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToEndOfScopeFlat()
	{
		auto stream = Scripting::Source::FromMemory("{ a b c }");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeNested()
	{
		auto stream = Scripting::Source::FromMemory("{ a { b } c }");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeDeeplyNested()
	{
		auto stream = Scripting::Source::FromMemory("{ a { b { c } d } e }");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeParentheses()
	{
		auto stream = Scripting::Source::FromMemory("( a + b )");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenParen, Scripting::TokenType::CloseParen);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseParen);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeUnbalancedReturnsEOF()
	{
		auto stream = Scripting::Source::FromMemory("{ a b c");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeEmptyScope()
	{
		auto stream = Scripting::Source::FromMemory("{ }");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeMixedNesting()
	{
		auto stream = Scripting::Source::FromMemory("{ a ( b ) c }");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeMismatchedTypesReturnsEOF()
	{
		// Tracking braces but only parens exist -> never returns to depth 0.
		auto stream = Scripting::Source::FromMemory("( a b c )");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToEndOfScope(lexer, Scripting::TokenType::OpenCurl, Scripting::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	// =====================================================================
	// Statement Boundary Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToStatementBoundarySemicolon()
	{
		auto stream = Scripting::Source::FromMemory("x + y ; z");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::SemiColon);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToStatementBoundaryCloseCurl()
	{
		auto stream = Scripting::Source::FromMemory("x y } z");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToStatementBoundaryEOF()
	{
		auto stream = Scripting::Source::FromMemory("x y z");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToStatementBoundaryAlreadyAtSemicolon()
	{
		auto stream = Scripting::Source::FromMemory("; x y");
		Scripting::Lexer lexer(*stream, 0);

		const auto& result = Scripting::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == Scripting::TokenType::SemiColon);
		TEST_PASSED();
	}
}
