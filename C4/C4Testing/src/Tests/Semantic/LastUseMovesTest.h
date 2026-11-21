#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Last-use moves (D-05, A10), decided on the body's MIR (Backend/MIR/MirMoves): which copies of a variable become moves,
	// and the drop flag that keeps a moved-from variable's cleanup from destroying the object again.
	CLASS(GROUP = Testing) LastUseMovesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LastUseMovesTest);

		TEST_FUNC(Semantic.LastUseMoves)
		void LastCopyMovesWithADropFlag();
		TEST_FUNC(Semantic.LastUseMoves)
		void LaterUseKeepsTheCopy();
		TEST_FUNC(Semantic.LastUseMoves)
		void ReferenceArgumentDoesNotEscape();
		TEST_FUNC(Semantic.LastUseMoves)
		void TakenAddressEscapes();
		TEST_FUNC(Semantic.LastUseMoves)
		void LoopKeepsTheCopy();
		TEST_FUNC(Semantic.LastUseMoves)
		void DeferUseKeepsTheCopy();
		TEST_FUNC(Semantic.LastUseMoves)
		void GotoFollowsTheFlow();
		TEST_FUNC(Semantic.LastUseMoves)
		void ExclusiveBranchesMoveInEach();
		TEST_FUNC(Semantic.LastUseMoves)
		void AnExceptionPathKeepsTheCopy();
		TEST_FUNC(Semantic.LastUseMoves)
		void TriviallyCopiedTypesHaveNoTransfers();
		TEST_FUNC(Semantic.LastUseMoves)
		void AssignedVariableStillMoves();
	};
} // namespace Testing
