#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R7: a file whose braces do not pair up is mended by its indentation before it is parsed (Lexer/BraceRecovery.h).
	CLASS(GROUP = Testing) BraceRecoveryTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BraceRecoveryTest);

		TEST_FUNC(Lexer.BraceRecovery)
		void PairedBracesAreLeftAlone();

		TEST_FUNC(Lexer.BraceRecovery)
		void EmptyFileHasNothingToMend();

		TEST_FUNC(Lexer.BraceRecovery)
		void OpenBlockAtEndClosesBeforeOutdentedLine();

		TEST_FUNC(Lexer.BraceRecovery)
		void OpenBlockWithNothingAfterClosesAtEnd();

		TEST_FUNC(Lexer.BraceRecovery)
		void LessIndentedCloserClosesInnerBlockFirst();

		TEST_FUNC(Lexer.BraceRecovery)
		void SameLineBracesAreNotOutdents();

		TEST_FUNC(Lexer.BraceRecovery)
		void CommentAtColumnOneIsNoOutdent();

		TEST_FUNC(Lexer.BraceRecovery)
		void DeeperIndentedCloserIsDropped();

		TEST_FUNC(Lexer.BraceRecovery)
		void StrayCloserAtRootIsLeftToTheParser();

		TEST_FUNC(Lexer.BraceRecovery)
		void LexerInsertsTheClosingBrace();
	};
}
