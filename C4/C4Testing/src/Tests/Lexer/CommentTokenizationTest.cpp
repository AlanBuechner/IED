#include "CommentTokenizationTest.h"

namespace Testing
{
	// Consolidated: single-line and multi-line comments via Token state machine
	void CommentTokenizationTest::TestAllComments()
	{
		// Single-line comment with content, closed by newline
		C4Compiler::Token token;
		token.AppendChar('/');
		auto result = token.AppendChar('/');
		TEST_ASSERT(result == C4Compiler::Token::Consume);
		TEST_ASSERT(token.GetType() == C4Compiler::TokenType::Comment);

		token.AppendChar('H');
		token.AppendChar('e');
		token.AppendChar('l');
		token.AppendChar('l');
		token.AppendChar('o');

		result = token.AppendChar('\n');
		TEST_ASSERT(result == C4Compiler::Token::ConsumeAndClose);

		// Multi-line comment with content, closed by */
		C4Compiler::Token token2;
		token2.AppendChar('/');
		result = token2.AppendChar('*');
		TEST_ASSERT(result == C4Compiler::Token::Consume);
		TEST_ASSERT(token2.GetType() == C4Compiler::TokenType::MultiComment);

		token2.AppendChar('H');
		token2.AppendChar('e');
		token2.AppendChar('l');
		token2.AppendChar('l');
		token2.AppendChar('o');

		result = token2.AppendChar('*');
		TEST_ASSERT(result == C4Compiler::Token::Consume);
		result = token2.AppendChar('/');
		TEST_ASSERT(result == C4Compiler::Token::ConsumeAndClose);

		// `/*/` is still open (the `*` belongs to `/*`, BUG-123); the empty comment is `/**/`.
		C4Compiler::Token token3;
		token3.AppendChar('/');
		token3.AppendChar('*');
		TEST_ASSERT(token3.GetType() == C4Compiler::TokenType::MultiComment);
		TEST_ASSERT(token3.AppendChar('/') == C4Compiler::Token::Consume);

		C4Compiler::Token empty;
		empty.AppendChar('/');
		empty.AppendChar('*');
		empty.AppendChar('*');
		TEST_ASSERT(empty.AppendChar('/') == C4Compiler::Token::ConsumeAndClose);

		// Multi-line comment ignores newlines (unlike single-line)
		C4Compiler::Token token4;
		token4.AppendChar('/');
		token4.AppendChar('*');
		result = token4.AppendChar('\n');
		TEST_ASSERT(result == C4Compiler::Token::Consume); // newline is just Consume in multi-comment

		token4.AppendChar(' ');
		token4.AppendChar('s');
		token4.AppendChar('p');
		token4.AppendChar('a');
		token4.AppendChar('n');
		token4.AppendChar('n');
		token4.AppendChar('i');
		token4.AppendChar('n');
		token4.AppendChar('g');

		result = token4.AppendChar('\n');
		TEST_ASSERT(result == C4Compiler::Token::Consume);

		token4.AppendChar('*');
		result = token4.AppendChar('/');
		TEST_ASSERT(result == C4Compiler::Token::ConsumeAndClose);

		// Lexer integration: single-line comment followed by identifier
		// Use NextToken() because NextCompoundToken() skips comments.
		auto stream = C4Compiler::Source::FromMemory("// hello\nx");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t1 = lexer.NextToken();
		TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Comment);
		auto& t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Identifier);

		TEST_PASSED();
	}

		void CommentTokenizationTest::TestLineCommentEndLocation()
		{
			// A line comment is closed BY its terminating newline: the lexer consumes it (ConsumeAndClose),
			// so the exclusive end offset points one past the '\n' - same convention as other tokens whose
			// closing delimiter is part of their span. The next token starts exactly where it ends.
			auto stream = C4Compiler::Source::FromMemory("// hello\nx");
			C4Compiler::Lexer lexer(*stream, 0);
			auto& t1 = lexer.NextToken();
			TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Comment);
			TEST_ASSERT_EQ((size_t)t1.GetStartLocation().byteOffset, (size_t)0);
			TEST_ASSERT_EQ((size_t)t1.GetEndLocation().byteOffset, (size_t)9); // exclusive end one past the newline

			auto& t2 = lexer.NextCompoundToken(); // skips whitespace; next token is 'x'
			TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Identifier);
			TEST_ASSERT_EQ((size_t)t2.GetStartLocation().byteOffset, (size_t)9);

			TEST_PASSED();
		}

	void CommentTokenizationTest::TestForwardSlashThenNotComment()
	{
		// ForwardSlash followed by something other than '/' or '*' remains ForwardSlash
		C4Compiler::Token token;
		token.AppendChar('/');
		TEST_ASSERT(token.GetType() == C4Compiler::TokenType::ForwardSlash);

		auto result = token.AppendChar('x');
		// 'x' is not a promotion char for ForwardSlash, so it Rejects
		TEST_ASSERT(result == C4Compiler::Token::Reject);
		// Token type should still be ForwardSlash
		TEST_ASSERT(token.GetType() == C4Compiler::TokenType::ForwardSlash);

		// Lexer integration: / followed by non-comment char is division
		auto stream = C4Compiler::Source::FromMemory("/ x");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::ForwardSlash);
		auto& t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	void CommentTokenizationTest::TestSlashStarSlashStaysOpen()
	{
		// BUG-123: the `*` of `/*` does not also start the closing `*/`.
		auto stream = C4Compiler::Source::FromMemory("/*/ x = 2; */ y /**/ z");
		C4Compiler::Lexer lexer(*stream, 0);
		const C4Compiler::Token& first = lexer.NextCompoundToken();
		TEST_ASSERT(first.GetType() == C4Compiler::TokenType::Identifier && first.GetText() == "y");
		const C4Compiler::Token& second = lexer.NextCompoundToken();
		TEST_ASSERT(second.GetType() == C4Compiler::TokenType::Identifier && second.GetText() == "z");
		TEST_ASSERT(lexer.Diagnostics().empty());
		TEST_PASSED();
	}

	void CommentTokenizationTest::TestUnclosedBlockCommentIsReported()
	{
		auto stream = C4Compiler::Source::FromMemory("x /* never closed");
		C4Compiler::Lexer lexer(*stream, 0);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == C4Compiler::TokenType::Identifier);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == C4Compiler::TokenType::EndOfFile);
		TEST_ASSERT(lexer.Diagnostics().size() == 1);
		const auto& diagnostic = lexer.Diagnostics().begin()->second;
		TEST_ASSERT(diagnostic.message == "this '/*' comment has no closing '*/'");
		TEST_ASSERT(diagnostic.location.column == 3);
		TEST_PASSED();
	}
}
