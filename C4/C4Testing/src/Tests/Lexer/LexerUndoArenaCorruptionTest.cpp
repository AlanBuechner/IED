#include "LexerUndoArenaCorruptionTest.h"
#include "SourceManager/Source.h"
#include <vector>

namespace Testing
{
	void LexerUndoArenaCorruptionTest::TestUndoInnerThenApplyOuter_LazyAllocation()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e f");
		C4Compiler::Lexer lexer(*stream, 0);

		// No preloading - tokens are lazily allocated as NextToken advances.
		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1
		lexer.NextCompoundToken();                              // "b", offset = 2

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 2
		lexer.NextCompoundToken();                              // "c", offset = 3
		lexer.NextCompoundToken();                              // "d", offset = 4

		// Undo to inner checkpoint B (offset 2)
		C4Compiler::LexerTestAccess::Undo(lexer);               // offset = 2, stack: [A]
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "c");

		// Apply outer checkpoint A: discard tokens consumed since A (offset 2),
		// leaving "c" at the front. Arena corruption would destroy tokens here.
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);           // pop A, discard 2 tokens
		auto afterApply = lexer.Peek();
		TEST_ASSERT(afterApply.GetText() == "c");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestThreeLevelUndoMiddleApplyOuter_LazyAllocation()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e f g h");
		C4Compiler::Lexer lexer(*stream, 0);

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 1
		lexer.NextCompoundToken();                              // "b", offset = 2

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // C at offset 2
		lexer.NextCompoundToken();                              // "c", offset = 3
		lexer.NextCompoundToken();                              // "d", offset = 4

		// Undo C -> offset 2, stack: [A, B]
		C4Compiler::LexerTestAccess::Undo(lexer);
		auto atC = lexer.Peek();
		TEST_ASSERT(atC.GetText() == "c");

		// Undo B -> offset 1, stack: [A]
		C4Compiler::LexerTestAccess::Undo(lexer);
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "b");

		// Apply outer A: discard 1 token, leaving "b" at front.
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);
		auto afterApply = lexer.Peek();
		TEST_ASSERT(afterApply.GetText() == "b");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestUndoInnerNoAdvance_LazyAllocation()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e");
		C4Compiler::Lexer lexer(*stream, 0);

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1
		lexer.NextCompoundToken();                              // "b", offset = 2

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 2
		auto peeked = lexer.Peek();                            // allocates "c" lazily, offset still 2
		TEST_ASSERT(peeked.GetText() == "c");

		// Undo to B (offset 2) - no offset change, arena must not shrink.
		C4Compiler::LexerTestAccess::Undo(lexer);               // offset = 2, stack: [A]

		// Apply outer A: discard 2 tokens from front, leaving "c" at position 0.
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);
		auto afterApply = lexer.Peek();
		TEST_ASSERT(afterApply.GetText() == "c");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestNestedUndoThenConsumeAllTokens_LazyAllocation()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e f g h");
		C4Compiler::Lexer lexer(*stream, 0);

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 1
		lexer.NextCompoundToken();                              // "b", offset = 2

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // C at offset 2
		lexer.NextCompoundToken();                              // "c", offset = 3
		lexer.NextCompoundToken();                              // "d", offset = 4

		// Undo all checkpoints back to start
		C4Compiler::LexerTestAccess::Undo(lexer);               // undo C, offset = 2
		auto atC = lexer.Peek();
		TEST_ASSERT(atC.GetText() == "c");

		C4Compiler::LexerTestAccess::Undo(lexer);               // undo B, offset = 1
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "b");

		C4Compiler::LexerTestAccess::Undo(lexer);               // undo A, offset = 0
		auto atA = lexer.Peek();
		TEST_ASSERT(atA.GetText() == "a");

		// Consume all tokens - every token must still be valid.
		std::vector<Core::String> consumed;
		for (int i = 0; i < 8; i++) {
			auto tok = lexer.NextCompoundToken();
			if (tok.GetType() == C4Compiler::TokenType::EndOfFile) break;
			consumed.push_back(tok.GetText());
		}

		TEST_ASSERT(consumed.size() == 8);
		TEST_ASSERT(consumed[0] == "a");
		TEST_ASSERT(consumed[3] == "d");
		TEST_ASSERT(consumed[7] == "h");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestApplyInnerThenApplyOuter_LazyAllocation()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e f");
		C4Compiler::Lexer lexer(*stream, 0);

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1
		lexer.NextCompoundToken();                              // "b", offset = 2
		lexer.NextCompoundToken();                              // "c", offset = 3

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 3
		lexer.NextCompoundToken();                              // "d", offset = 4
		lexer.NextCompoundToken();                              // "e", offset = 5

		// Apply inner B: discard tokens from checkpoint B to current offset, remaining starts at "f".
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);
		auto afterInner = lexer.Peek();
		TEST_ASSERT(afterInner.GetText() == "f");

		// Apply outer A: no discard (offset at checkpoint), stack empty.
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);
		auto afterOuter = lexer.Peek();
		TEST_ASSERT(afterOuter.GetText() == "f");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestUndoMiddleAdvanceLazyAlloc_UndoOuter()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e f g");
		C4Compiler::Lexer lexer(*stream, 0);

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1
		lexer.NextCompoundToken();                              // "b", offset = 2

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 2
		lexer.NextCompoundToken();                              // "c", offset = 3
		lexer.NextCompoundToken();                              // "d", offset = 4

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // C at offset 4
		lexer.NextCompoundToken();                              // "e", offset = 5

		// Undo C -> offset 4, stack: [A, B]
		C4Compiler::LexerTestAccess::Undo(lexer);
		auto atC = lexer.Peek();
		TEST_ASSERT(atC.GetText() == "e");

		// Advance further within B context - lazy alloc of "f"
		lexer.NextCompoundToken();                              // "e", offset = 5
		auto advanced = lexer.Peek();                           // lazy alloc of "f"
		TEST_ASSERT(advanced.GetText() == "f");

		// Undo B -> offset 2, stack: [A]
		C4Compiler::LexerTestAccess::Undo(lexer);
		auto atB = lexer.Peek();
		TEST_ASSERT(atB.GetText() == "c");

		// Undo A -> offset 0, stack: []
		C4Compiler::LexerTestAccess::Undo(lexer);
		auto atA = lexer.Peek();
		TEST_ASSERT(atA.GetText() == "a");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestMixedUndoApply_LazyAllocation()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c d e f g h i j");
		C4Compiler::Lexer lexer(*stream, 0);

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // A at offset 0
		lexer.NextCompoundToken();                              // "a", offset = 1

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // B at offset 1
		lexer.NextCompoundToken();                              // "b", offset = 2
		lexer.NextCompoundToken();                              // "c", offset = 3

		C4Compiler::LexerTestAccess::StartUndo(lexer);          // C at offset 3
		lexer.NextCompoundToken();                              // "d", offset = 4
		lexer.NextCompoundToken();                              // "e", offset = 5

		// Apply innermost C: pops checkpoint, no arena compaction. m_Offset stays 5.
		// Peek() at index 5 -> "f"
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);
		auto afterApplyC = lexer.Peek();
		TEST_ASSERT(afterApplyC.GetText() == "f");

		// Undo B: m_Offset = checkpoint.offset = 1. Arena is NOT compacted, so index 1 -> "b".
		C4Compiler::LexerTestAccess::Undo(lexer);
		auto afterUndoB = lexer.Peek();
		TEST_ASSERT(afterUndoB.GetText() == "b");

		// Advance: NextCompoundToken consumes arena[1] ("b"), offset becomes 2, returns "b"
		auto nextTok = lexer.NextCompoundToken();
		TEST_ASSERT(nextTok.GetText() == "b");

		// Apply A: pops checkpoint at offset 0, no compaction. m_Offset stays 2.
		C4Compiler::LexerTestAccess::ApplyUndo(lexer);
		auto afterApplyA = lexer.Peek();
		TEST_ASSERT(afterApplyA.GetText() == "c");

		std::vector<Core::String> remaining;
		while (true) {
			auto tok = lexer.NextCompoundToken();
			if (tok.GetType() == C4Compiler::TokenType::EndOfFile) break;
			remaining.push_back(tok.GetText());
		}
		TEST_ASSERT(remaining.size() == 8);
		TEST_ASSERT(remaining[0] == "c");
		TEST_ASSERT(remaining[7] == "j");
		TEST_PASSED();
	}

	void LexerUndoArenaCorruptionTest::TestPeekedTokensKeepTheirAddresses()
	{
		std::string text;
		for (int i = 0; i < 5000; ++i) text += "t" + std::to_string(i) + " ";
		auto stream = C4Compiler::Source::FromMemory(text);
		C4Compiler::Lexer lexer(*stream, 0);
		const C4Compiler::Token& first = lexer.Peek(0);
		const C4Compiler::Token& second = lexer.Peek(1);
		TEST_ASSERT(lexer.Peek(4500).GetText() == "t4500");
		TEST_ASSERT(&lexer.Peek(0) == &first && &lexer.Peek(1) == &second);
		TEST_ASSERT(first.GetText() == "t0" && second.GetText() == "t1");
		TEST_PASSED();
	}
}
