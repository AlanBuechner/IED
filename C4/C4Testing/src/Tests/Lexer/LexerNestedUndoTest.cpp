#include "LexerNestedUndoTest.h"
#include "SourceManager/Source.h"
#include <vector>

namespace Testing
{
	void LexerNestedUndoTest::TestDeepNestedUndo()
	{
		// Three levels of StartUndo, then unwind innermost to outermost.
		auto stream = Scripting::Source::FromMemory("a b c d e f g");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(6);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 1
		lexer.NextToken();          // offset = 2

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint C at offset 2
		lexer.NextToken();          // offset = 3

		// Undo innermost (C) -> back to offset 2
		Scripting::LexerTestAccess::Undo(lexer);
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetType() == Scripting::TokenType::Identifier);
		// index 2 is "c"

		// Undo middle (B) -> back to offset 1
		Scripting::LexerTestAccess::Undo(lexer);
		auto atA = lexer.Peek();
		// index 1 is "b"
		TEST_ASSERT(atA.GetType() == Scripting::TokenType::Identifier);

		// Undo outermost (A) -> back to offset 0
		Scripting::LexerTestAccess::Undo(lexer);
		auto atStart = lexer.Peek();
		// index 0 is "a"
		TEST_ASSERT(atStart.GetType() == Scripting::TokenType::Identifier);

		// Verify the actual token text values are correct
		TEST_ASSERT(atStart.GetText() == "a");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestNestedUndoThenAdvanceAgain()
	{
		// Undo to middle checkpoint, then advance further within outer context.
		auto stream = Scripting::Source::FromMemory("a b c d e");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(4);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 2
		lexer.NextToken();          // offset = 3
		lexer.NextToken();          // offset = 4

		// Undo to checkpoint B (offset 2)
		Scripting::LexerTestAccess::Undo(lexer);
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "c");

		// Advance again within outer context A
		lexer.NextToken();          // offset = 3
		auto afterAdvance = lexer.Peek();
		TEST_ASSERT(afterAdvance.GetText() == "d");

		// Undo to checkpoint A (offset 0)
		Scripting::LexerTestAccess::Undo(lexer);
		auto atA = lexer.Peek();
		TEST_ASSERT(atA.GetText() == "a");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestNestedApplyUndo()
	{
		// Two levels: apply inner, tokens up to current offset are discarded.
		auto stream = Scripting::Source::FromMemory("a b c d e f");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(5);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2
		lexer.NextToken();          // offset = 3

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 3
		lexer.NextToken();          // offset = 4
		lexer.NextToken();          // offset = 5

		// Apply inner (B): pops B, discards 5 tokens from front, offset = 0
		Scripting::LexerTestAccess::ApplyUndo(lexer);

		// Remaining deque starts at original index 5 ("f")
		auto current = lexer.Peek();
		TEST_ASSERT(current.GetText() == "f");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestDeepNestedApplyUndo()
	{
		// Three levels: apply innermost, verify remaining tokens.
		auto stream = Scripting::Source::FromMemory("a b c d e f g h");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(7);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 1
		lexer.NextToken();          // offset = 2

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint C at offset 2
		lexer.NextToken();          // offset = 3

		// Apply innermost (C): pops C, discards 3 tokens from front, offset = 0
		Scripting::LexerTestAccess::ApplyUndo(lexer);

		// Remaining deque starts at original index 3 ("d")
		auto current = lexer.Peek();
		TEST_ASSERT(current.GetText() == "d");

		// Outer checkpoints B and A are still on the stack but offset is 0,
		// so further ApplyUndo calls are no-ops for discarding.
		Scripting::LexerTestAccess::ApplyUndo(lexer);          // apply B: pops B, offset=0 -> no discard
		auto afterB = lexer.Peek();
		TEST_ASSERT(afterB.GetText() == "d");

		Scripting::LexerTestAccess::ApplyUndo(lexer);          // apply A: pops A, offset=0 -> no discard
		auto afterA = lexer.Peek();
		TEST_ASSERT(afterA.GetText() == "d");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestUndoInnerThenApplyOuter()
	{
		// Undo to inner checkpoint, then apply outer checkpoint.
		auto stream = Scripting::Source::FromMemory("a b c d e f");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(5);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2
		lexer.NextToken();          // offset = 3

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 3
		lexer.NextToken();          // offset = 4
		lexer.NextToken();          // offset = 5

		// Undo to inner checkpoint B (offset 3)
		Scripting::LexerTestAccess::Undo(lexer);               // offset = 3, stack: [0]
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "d");

		// Apply outer checkpoint A: pops A, discards m_UndoOffset (3) tokens, offset = 0
		Scripting::LexerTestAccess::ApplyUndo(lexer);
		auto afterApply = lexer.Peek();
		// Tokens at original indices 0,1,2 were discarded; index 3 ("d") is now at front
		TEST_ASSERT(afterApply.GetText() == "d");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestApplyInnerThenApplyOuter()
	{
		// Apply inner checkpoint first, then apply outer checkpoint.
		auto stream = Scripting::Source::FromMemory("a b c d e f");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(5);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2
		lexer.NextToken();          // offset = 3

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 3
		lexer.NextToken();          // offset = 4
		lexer.NextToken();          // offset = 5

		// Apply inner (B): pops B, discards 5 tokens from front, offset = 0, stack: [0]
		Scripting::LexerTestAccess::ApplyUndo(lexer);
		auto afterInner = lexer.Peek();
		TEST_ASSERT(afterInner.GetText() == "f");

		// Apply outer (A): pops A, offset is 0 so no discard, stack: []
		Scripting::LexerTestAccess::ApplyUndo(lexer);
		auto afterOuter = lexer.Peek();
		TEST_ASSERT(afterOuter.GetText() == "f");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestPeekLookaheadAfterNestedUndo()
	{
		// Verify Peek with lookahead offset works correctly after nested undo.
		auto stream = Scripting::Source::FromMemory("a b c d e f");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(5);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2
		lexer.NextToken();          // offset = 3

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 3
		lexer.NextToken();          // offset = 4
		lexer.NextToken();          // offset = 5

		// Undo to inner checkpoint B (offset 3)
		Scripting::LexerTestAccess::Undo(lexer);               // offset = 3, stack: [0]

		// Peek lookahead from restored position (offset 3)
		auto peek0 = lexer.Peek(0); // index 0 + offset 3 = "d"
		TEST_ASSERT(peek0.GetText() == "d");

		auto peek1 = lexer.Peek(1); // index 1 + offset 3 = "e"
		// Need to ensure enough tokens are buffered for lookahead beyond current
		auto peek2 = lexer.Peek(2); // index 2 + offset 3 = "f"
		TEST_ASSERT(peek2.GetText() == "f");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestUndoToMiddleThenAdvanceThenUndoOuter()
	{
		// Three levels: undo innermost, advance further, then undo to outermost.
		auto stream = Scripting::Source::FromMemory("a b c d e f g");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(6);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 2
		lexer.NextToken();          // offset = 3
		lexer.NextToken();          // offset = 4

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint C at offset 4
		lexer.NextToken();          // offset = 5
		lexer.NextToken();          // offset = 6

		// Undo innermost (C) -> offset = 4
		Scripting::LexerTestAccess::Undo(lexer);
		auto atC = lexer.Peek();
		TEST_ASSERT(atC.GetText() == "e");

		// Advance further within B context
		lexer.NextToken();          // offset = 5
		auto advanced = lexer.Peek();
		TEST_ASSERT(advanced.GetText() == "f");

		// Undo to middle (B) -> offset = 2
		Scripting::LexerTestAccess::Undo(lexer);
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "c");

		// Undo to outermost (A) -> offset = 0
		Scripting::LexerTestAccess::Undo(lexer);
		auto atA = lexer.Peek();
		TEST_ASSERT(atA.GetText() == "a");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestNestedUndoWithGetNextToken()
	{
		// Use GetNextToken (Peek + NextToken combined) within nested undo context.
		auto stream = Scripting::Source::FromMemory("a b c d e");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(4);              // preload all identifiers + EOF

		auto t1 = lexer.NextCompoundToken(); // Peek + NextToken, offset = 1
		TEST_ASSERT(t1.GetText() == "a");

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 1
		auto t2 = lexer.NextCompoundToken(); // Peek + NextToken, offset = 2
		TEST_ASSERT(t2.GetText() == "b");

		// Undo to checkpoint B (offset 1)
		Scripting::LexerTestAccess::Undo(lexer);

		auto t3 = lexer.NextCompoundToken(); // returns token at offset 1 ("b"), then offset = 2
		TEST_ASSERT(t3.GetText() == "b");

		auto current = lexer.Peek();    // offset is 2, so Peek(0) -> index 2 = "c"
		TEST_ASSERT(current.GetText() == "c");
		TEST_PASSED();
	}

	void LexerNestedUndoTest::TestTokensAccessibleAfterFullNestedUndoChain()
	{
		// Full undo chain then verify all original tokens remain accessible.
		auto stream = Scripting::Source::FromMemory("a b c d e f g h i j");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint A at offset 0
		lexer.Peek(9);              // preload all identifiers + EOF
		lexer.NextToken();          // offset = 1
		lexer.NextToken();          // offset = 2

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint B at offset 2
		lexer.NextToken();          // offset = 3
		lexer.NextToken();          // offset = 4

		Scripting::LexerTestAccess::StartUndo(lexer);          // checkpoint C at offset 4
		lexer.NextToken();          // offset = 5
		lexer.NextToken();          // offset = 6

		// Undo all the way back through each level
		Scripting::LexerTestAccess::Undo(lexer);               // C -> offset = 4
		auto atC = lexer.Peek();
		TEST_ASSERT(atC.GetText() == "e");

		Scripting::LexerTestAccess::Undo(lexer);               // B -> offset = 2
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "c");

		Scripting::LexerTestAccess::Undo(lexer);               // A -> offset = 0
		auto atA = lexer.Peek();
		TEST_ASSERT(atA.GetText() == "a");

		// Now consume all 10 tokens to verify none were lost
		std::vector<Core::String> consumed;
		for (int i = 0; i < 10; i++) {
			auto tok = lexer.NextCompoundToken();
			consumed.push_back(tok.GetText());
		}

		TEST_ASSERT(consumed[0] == "a");
		TEST_ASSERT(consumed[4] == "e");
		TEST_ASSERT(consumed[9] == "j");
		TEST_PASSED();
	}
}
