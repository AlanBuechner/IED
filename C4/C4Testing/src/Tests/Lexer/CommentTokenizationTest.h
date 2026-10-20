#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"

namespace Testing
{
	CLASS(GROUP = Testing) CommentTokenizationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CommentTokenizationTest);

		// Consolidated: single-line and multi-line comments in one pass
		TEST_FUNC(Lexer.Comments.AllComments)
		void TestAllComments();

		// ForwardSlash followed by non-comment character is division, not a comment
		TEST_FUNC(Lexer.Comments.ForwardSlashNotComment)
		void TestForwardSlashThenNotComment();

		// A line comment's exclusive end offset points one past its terminating newline (the lexer consumes it to close the token)
		TEST_FUNC(Lexer.Comments.LineCommentEndLocation)
		void TestLineCommentEndLocation();

		TEST_FUNC(Lexer.Comments.BlockCommentBounds)
		void TestSlashStarSlashStaysOpen();

		TEST_FUNC(Lexer.Comments.BlockCommentBounds)
		void TestUnclosedBlockCommentIsReported();
	};
}
