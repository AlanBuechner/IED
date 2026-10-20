#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"

namespace Testing
{
	CLASS(GROUP = Testing) LexerUndoArenaCorruptionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LexerUndoArenaCorruptionTest);

		// Undo inner scope, then apply outer scope - tokens lazily allocated
		// inside the inner scope must survive the inner Undo for the outer
		// ApplyUndo to compact correctly.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestUndoInnerThenApplyOuter_LazyAllocation();

		// Three-level nesting with lazy allocation, undo middle then apply outer.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestThreeLevelUndoMiddleApplyOuter_LazyAllocation();

		// Undo inner scope without advancing offset - arena must not shrink
		// and destroy tokens allocated by Peek in the outer scope.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestUndoInnerNoAdvance_LazyAllocation();

		// Nested undo with lazy allocation, then consume all remaining tokens
		// to verify none were corrupted or lost.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestNestedUndoThenConsumeAllTokens_LazyAllocation();

		// Apply inner checkpoint with lazy allocation, then apply outer checkpoint.
		// Both compactions must produce correct results.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestApplyInnerThenApplyOuter_LazyAllocation();

		// Undo to middle checkpoint, advance further (lazy alloc), then undo outer.
		// Verifies that tokens allocated after the inner undo are still accessible.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestUndoMiddleAdvanceLazyAlloc_UndoOuter();

		// Mixed Undo and ApplyUndo with lazy allocation at each level.
		TEST_FUNC(Lexer.Undo.Arena)
		void TestMixedUndoApply_LazyAllocation();
	};
}
