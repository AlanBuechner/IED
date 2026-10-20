#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/LexerRecovery.h"

namespace Testing
{
	CLASS(GROUP = Testing) LexerRecoveryTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LexerRecoveryTest);

		// Predicate-based recovery
		TEST_FUNC(Lexer.Recovery)
		void TestRecoverUntilFindsToken();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverUntilStopsOnFirstMatch();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverUntilReturnsEOFWhenNotFound();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverUntilEmptyInput();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverUntilCompoundTokens();

		// Token-type recovery
		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToTokenTypeFindsTarget();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToTokenTypeAlreadyAtTarget();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToTokenTypeReturnsEOFWhenNotFound();

		// Any-token-type recovery
		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToAnyTokenTypeFindsFirstMatch();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToAnyTokenTypeMultipleTargets();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToAnyTokenTypeReturnsEOFWhenNotFound();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToAnyTokenTypeEmptyList();

		// Scope recovery
		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeFlat();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeNested();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeDeeplyNested();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeParentheses();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeUnbalancedReturnsEOF();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeEmptyScope();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeMixedNesting();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToEndOfScopeMismatchedTypesReturnsEOF();

		// Statement boundary recovery
		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToStatementBoundarySemicolon();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToStatementBoundaryCloseCurl();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToStatementBoundaryEOF();

		TEST_FUNC(Lexer.Recovery)
		void TestRecoverToStatementBoundaryAlreadyAtSemicolon();
	};
}
