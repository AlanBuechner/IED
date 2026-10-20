#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"

namespace Testing
{
	CLASS(GROUP = Testing) UndoMechanismTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::UndoMechanismTest);

		// Basic undo/rollback
		TEST_FUNC(Lexer.Undo)
		void TestUndoRestoresPreviousPosition();

		TEST_FUNC(Lexer.Undo)
		void TestApplyUndoCommitsRollback();

		// State management
		TEST_FUNC(Lexer.Undo)
		void TestPeekAfterUndoShowsRestoredToken();

		TEST_FUNC(Lexer.Undo)
		void TestMultipleUndosAndApply();

		TEST_FUNC(Lexer.Undo)
		void TestUndoDoesNotAffectQueueWithoutNextToken();

		TEST_FUNC(Lexer.Undo)
		void TestNestedUndoStartAndApply();
	};
}
