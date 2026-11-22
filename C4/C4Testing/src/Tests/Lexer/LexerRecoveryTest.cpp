#include "LexerRecoveryTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	// =====================================================================
	// Predicate-based Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverUntilFindsToken()
	{
		auto stream = C4Compiler::Source::FromMemory("x y 42 z");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverUntil(lexer, [](const C4Compiler::Token& t) -> bool
		{
			return t.GetType() == C4Compiler::TokenType::Number;
		});

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::Number);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilStopsOnFirstMatch()
	{
		auto stream = C4Compiler::Source::FromMemory("42 x y");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverUntil(lexer, [](const C4Compiler::Token& t) -> bool
		{
			return t.GetType() == C4Compiler::TokenType::Number;
		});

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::Number);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilReturnsEOFWhenNotFound()
	{
		auto stream = C4Compiler::Source::FromMemory("x y z");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverUntil(lexer, [](const C4Compiler::Token& t) -> bool
		{
			return t.GetType() == C4Compiler::TokenType::Number;
		});

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilEmptyInput()
	{
		auto stream = C4Compiler::Source::FromMemory("");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverUntil(lexer, [](const C4Compiler::Token& t) -> bool
		{
			return false;
		});

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverUntilCompoundTokens()
	{
		auto stream = C4Compiler::Source::FromMemory("x++ y==z");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverUntil(lexer, [](const C4Compiler::Token& t) -> bool
		{
			return t.GetType() == C4Compiler::TokenType::DoubleEqual;
		});

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::DoubleEqual);
		TEST_PASSED();
	}

	// =====================================================================
	// Token-Type Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToTokenTypeFindsTarget()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c { d");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToTokenType(lexer, C4Compiler::TokenType::OpenCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::OpenCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToTokenTypeAlreadyAtTarget()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a b");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToTokenType(lexer, C4Compiler::TokenType::OpenCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::OpenCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToTokenTypeReturnsEOFWhenNotFound()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToTokenType(lexer, C4Compiler::TokenType::OpenCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	// =====================================================================
	// Any-Token-Type Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeFindsFirstMatch()
	{
		auto stream = C4Compiler::Source::FromMemory("x y + z");
		C4Compiler::Lexer lexer(*stream, 0);

		Core::Vector<C4Compiler::TokenType> targets;
		targets.Push(C4Compiler::TokenType::Minus);
		targets.Push(C4Compiler::TokenType::Plus);

		const auto& result = C4Compiler::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::Plus);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeMultipleTargets()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c { }");
		C4Compiler::Lexer lexer(*stream, 0);

		Core::Vector<C4Compiler::TokenType> targets;
		targets.Push(C4Compiler::TokenType::CloseCurl);
		targets.Push(C4Compiler::TokenType::OpenCurl);

		const auto& result = C4Compiler::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::OpenCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeReturnsEOFWhenNotFound()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c");
		C4Compiler::Lexer lexer(*stream, 0);

		Core::Vector<C4Compiler::TokenType> targets;
		targets.Push(C4Compiler::TokenType::OpenCurl);
		targets.Push(C4Compiler::TokenType::CloseCurl);

		const auto& result = C4Compiler::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToAnyTokenTypeEmptyList()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c");
		C4Compiler::Lexer lexer(*stream, 0);

		Core::Vector<C4Compiler::TokenType> targets;

		const auto& result = C4Compiler::LexerRecovery::RecoverToAnyTokenType(lexer, targets);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	// =====================================================================
	// Scope Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToEndOfScopeFlat()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a b c }");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeNested()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a { b } c }");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeDeeplyNested()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a { b { c } d } e }");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeParentheses()
	{
		auto stream = C4Compiler::Source::FromMemory("( a + b )");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenParen, C4Compiler::TokenType::CloseParen);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseParen);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeUnbalancedReturnsEOF()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a b c");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeEmptyScope()
	{
		auto stream = C4Compiler::Source::FromMemory("{ }");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeMixedNesting()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a ( b ) c }");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToEndOfScopeMismatchedTypesReturnsEOF()
	{
		// Tracking braces but only parens exist -> never returns to depth 0.
		auto stream = C4Compiler::Source::FromMemory("( a b c )");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToEndOfScope(lexer, C4Compiler::TokenType::OpenCurl, C4Compiler::TokenType::CloseCurl);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	// =====================================================================
	// Statement Boundary Recovery
	// =====================================================================

	void LexerRecoveryTest::TestRecoverToStatementBoundarySemicolon()
	{
		auto stream = C4Compiler::Source::FromMemory("x + y ; z");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::SemiColon);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToStatementBoundaryCloseCurl()
	{
		auto stream = C4Compiler::Source::FromMemory("x y } z");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToStatementBoundaryEOF()
	{
		auto stream = C4Compiler::Source::FromMemory("x y z");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_PASSED();
	}

	void LexerRecoveryTest::TestRecoverToStatementBoundaryAlreadyAtSemicolon()
	{
		auto stream = C4Compiler::Source::FromMemory("; x y");
		C4Compiler::Lexer lexer(*stream, 0);

		const auto& result = C4Compiler::LexerRecovery::RecoverToStatementBoundary(lexer);

		TEST_ASSERT(result.GetType() == C4Compiler::TokenType::SemiColon);
		TEST_PASSED();
	}
}
