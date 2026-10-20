#include "UndoMechanismTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	void UndoMechanismTest::TestUndoRestoresPreviousPosition()
	{
		// StartUndo saves checkpoint, NextToken advances within undo context,
		// Undo restores position back to the checkpoint.
		auto stream = Scripting::Source::FromMemory("a b c");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);
		// Preload enough tokens before advancing with NextToken
		lexer.Peek(2); // ensures at least 3 tokens in deque (indices 0,1,2)

		lexer.NextToken(); // skip 'a', offset = 1
		lexer.NextToken(); // skip 'b', offset = 2

		auto afterAdvance = lexer.Peek();
		TEST_ASSERT(afterAdvance.GetType() == Scripting::TokenType::Identifier);

		Scripting::LexerTestAccess::Undo(lexer); // restore to offset 0

		auto restored = lexer.Peek();
		TEST_ASSERT(restored.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void UndoMechanismTest::TestApplyUndoCommitsRollback()
	{
		// StartUndo -> advance -> ApplyUndo: consumed tokens are discarded.
		auto stream = Scripting::Source::FromMemory("a b c");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);
		// Preload enough tokens before advancing
		lexer.Peek(2); // ensures at least 3 tokens in deque

		lexer.NextToken(); // offset = 1 (token not popped from queue)
		lexer.NextToken(); // offset = 2

		Scripting::LexerTestAccess::ApplyUndo(lexer); // commit: discard 2 tokens, offset = 0

		auto current = lexer.Peek();
		TEST_ASSERT(current.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void UndoMechanismTest::TestPeekAfterUndoShowsRestoredToken()
	{
		// After undo, Peek should show the token at the restored position.
		auto stream = Scripting::Source::FromMemory("hello world");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);

		auto first = lexer.Peek();
		TEST_ASSERT(first.GetType() == Scripting::TokenType::Identifier);

		lexer.NextToken(); // offset = 1
		auto second = lexer.Peek();
		TEST_ASSERT(second.GetType() == Scripting::TokenType::Identifier);

		Scripting::LexerTestAccess::Undo(lexer); // back to offset 0
		auto restored = lexer.Peek();
		TEST_ASSERT(restored.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void UndoMechanismTest::TestMultipleUndosAndApply()
	{
		// Multiple NextToken calls within undo context, then apply.
		auto stream = Scripting::Source::FromMemory("1 2 3 4");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);
		// Preload enough tokens before advancing
		lexer.Peek(3); // ensures at least 4 tokens in deque

		lexer.NextToken(); // offset = 1
		lexer.NextToken(); // offset = 2
		lexer.NextToken(); // offset = 3

		Scripting::LexerTestAccess::ApplyUndo(lexer); // discard 3 tokens, offset = 0

		auto current = lexer.Peek();
		TEST_ASSERT(current.GetType() == Scripting::TokenType::Number);
		TEST_PASSED();
	}

	void UndoMechanismTest::TestUndoDoesNotAffectQueueWithoutNextToken()
	{
		// StartUndo then Undo without advancing should be a no-op for position.
		auto stream = Scripting::Source::FromMemory("x");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);
		Scripting::LexerTestAccess::Undo(lexer); // restore to offset 0 (was already 0)

		auto token = lexer.Peek();
		TEST_ASSERT(token.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void UndoMechanismTest::TestNestedUndoStartAndApply()
	{
		// Nested StartUndo: inner checkpoint, undo to inner, then apply outer.
		auto stream = Scripting::Source::FromMemory("a b c d");
		Scripting::Lexer lexer(*stream, 0);

		Scripting::LexerTestAccess::StartUndo(lexer);       // checkpoint A at offset 0
		// Preload tokens before advancing
		lexer.Peek(3);           // ensures at least 4 tokens in deque
		lexer.NextToken();       // offset = 1

		Scripting::LexerTestAccess::StartUndo(lexer);       // checkpoint B at offset 1
		lexer.NextToken();       // offset = 2

		Scripting::LexerTestAccess::Undo(lexer);            // restore to checkpoint B, offset = 1
		Scripting::LexerTestAccess::ApplyUndo(lexer);       // commit outer: pop A, m_UndoOffset=1, discard 1 token

		auto current = lexer.Peek();
		TEST_ASSERT(current.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}
}
