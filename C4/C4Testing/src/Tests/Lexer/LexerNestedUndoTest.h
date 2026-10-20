#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"

namespace Testing
{
	CLASS(GROUP = Testing) LexerNestedUndoTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LexerNestedUndoTest);

		// Deep nested undo - 3 levels, unwind innermost to outermost
		TEST_FUNC(Lexer.Undo.Nested)
		void TestDeepNestedUndo();

		// Nested undo then advance again from middle checkpoint
		TEST_FUNC(Lexer.Undo.Nested)
		void TestNestedUndoThenAdvanceAgain();

		// Two levels of ApplyUndo (nested apply)
		TEST_FUNC(Lexer.Undo.Nested)
		void TestNestedApplyUndo();

		// Three levels of ApplyUndo
		TEST_FUNC(Lexer.Undo.Nested)
		void TestDeepNestedApplyUndo();

		// Undo inner checkpoint, then apply outer checkpoint
		TEST_FUNC(Lexer.Undo.Nested)
		void TestUndoInnerThenApplyOuter();

		// Apply inner checkpoint, then apply outer checkpoint
		TEST_FUNC(Lexer.Undo.Nested)
		void TestApplyInnerThenApplyOuter();

		// Nested undo: peek at specific lookahead offsets after partial undo
		TEST_FUNC(Lexer.Undo.Nested)
		void TestPeekLookaheadAfterNestedUndo();

		// Undo to middle checkpoint, advance further, then undo again to outermost
		TEST_FUNC(Lexer.Undo.Nested)
		void TestUndoToMiddleThenAdvanceThenUndoOuter();

		// Nested undo with GetNextToken convenience method
		TEST_FUNC(Lexer.Undo.Nested)
		void TestNestedUndoWithGetNextToken();

		// Verify that after full nested undo chain, tokens are still accessible
		TEST_FUNC(Lexer.Undo.Nested)
		void TestTokensAccessibleAfterFullNestedUndoChain();
	};
}
