#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"

namespace Testing
{
	CLASS(GROUP = Testing) CompoundOperatorTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CompoundOperatorTest);

		// --- Greedy compound token formation via NextCompoundToken ---
		TEST_FUNC(Lexer.CompoundTokens.Arithmetic)
		void TestDoublePlus();

		TEST_FUNC(Lexer.CompoundTokens.Arithmetic)
		void TestDoubleMinus();

		TEST_FUNC(Lexer.CompoundTokens.Arithmetic)
		void TestArrow();

		TEST_FUNC(Lexer.CompoundTokens.Arithmetic)
		void TestDoubleStar();

		// --- Comparison compound operators ---
		TEST_FUNC(Lexer.CompoundTokens.Comparison)
		void TestDoubleEqual();

		TEST_FUNC(Lexer.CompoundTokens.Comparison)
		void TestLessOrEqual();

		TEST_FUNC(Lexer.CompoundTokens.Comparison)
		void TestGreaterOrEqual();

		TEST_FUNC(Lexer.CompoundTokens.Comparison)
		void TestExclamationEquals();

		// --- Double-angle and shift operators ---
		TEST_FUNC(Lexer.CompoundTokens.Shift)
		void TestDoubleLess();

		TEST_FUNC(Lexer.CompoundTokens.Shift)
		void TestDoubleGreater();

		TEST_FUNC(Lexer.CompoundTokens.Shift)
		void TestLessLessEquals();

		TEST_FUNC(Lexer.CompoundTokens.Shift)
		void TestGreaterGreaterEquals();

		// --- Assignment compound operators ---
		TEST_FUNC(Lexer.CompoundTokens.Assignment)
		void TestCompoundAssignments();

		// --- Bitwise and logical compound operators ---
		TEST_FUNC(Lexer.CompoundTokens.Logical)
		void TestDoubleAmpersandAndDoublePipe();

		TEST_FUNC(Lexer.CompoundTokens.Logical)
		void TestPipePipeEquals();

		// --- Special compound operators ---
		TEST_FUNC(Lexer.CompoundTokens.Special)
		void TestDoubleColonDotDotEllipsis();

		// --- Non-operator passthrough ---
		TEST_FUNC(Lexer.CompoundTokens.Passthrough)
		void TestNonOperatorsPassThroughUnchanged();

		// --- PeekCompound consistency ---
		TEST_FUNC(Lexer.CompoundTokens.Peek)
		void TestPeekCompoundMatchesNextCompoundToken();

		// --- Atomic vs compound token distinction ---
		TEST_FUNC(Lexer.CompoundTokens.AtomicVsCompound)
		void TestAtomicVsCompoundDistinction();

		// --- Mixed sequences with compounds and non-operators ---
		TEST_FUNC(Lexer.CompoundTokens.MixedSequence)
		void TestMixedCompoundAndNonOperatorSequence();

		// --- PeekCompound at multiple indices ---
		TEST_FUNC(Lexer.CompoundTokens.PeekMultiIndex)
		void TestPeekCompoundMultipleIndices();

		// --- Whitespace and comments end a compound (BUG-119) ---
		TEST_FUNC(Lexer.CompoundTokens.Adjacency)
		void TestWhitespaceEndsACompound();

		TEST_FUNC(Lexer.CompoundTokens.Adjacency)
		void TestCommentEndsACompound();

		TEST_FUNC(Lexer.CompoundTokens.Adjacency)
		void TestCompoundSpansItsCharacters();
	};
}
