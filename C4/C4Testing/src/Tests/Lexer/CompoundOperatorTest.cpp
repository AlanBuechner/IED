#include "CompoundOperatorTest.h"
#include <vector>

namespace Testing
{
	// --- Greedy compound token formation via NextCompoundToken ---

	void CompoundOperatorTest::TestDoublePlus()
	{
		auto stream = C4Compiler::Source::FromMemory("++");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoublePlus);
		TEST_ASSERT(t.GetText() == "++");
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestDoubleMinus()
	{
		auto stream = C4Compiler::Source::FromMemory("--");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleMinus);
		TEST_ASSERT(t.GetText() == "--");
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestArrow()
	{
		auto stream = C4Compiler::Source::FromMemory("->");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::Arrow);
		TEST_ASSERT(t.GetText() == "->");
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestDoubleStar()
	{
		auto stream = C4Compiler::Source::FromMemory("**");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleStar);
		TEST_ASSERT(t.GetText() == "**");
		TEST_PASSED();
	}

	// --- Comparison compound operators ---

	void CompoundOperatorTest::TestDoubleEqual()
	{
		auto stream = C4Compiler::Source::FromMemory("==");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleEqual);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestLessOrEqual()
	{
		auto stream = C4Compiler::Source::FromMemory("<=");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::LessOrEqual);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestGreaterOrEqual()
	{
		auto stream = C4Compiler::Source::FromMemory(">=");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::GreaterOrEqual);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestExclamationEquals()
	{
		auto stream = C4Compiler::Source::FromMemory("!=");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::ExclamationEquals);
		TEST_PASSED();
	}

	// --- Double-angle and shift operators ---

	void CompoundOperatorTest::TestDoubleLess()
	{
		auto stream = C4Compiler::Source::FromMemory("<<");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleLess);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestDoubleGreater()
	{
		auto stream = C4Compiler::Source::FromMemory(">>");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleGreater);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestLessLessEquals()
	{
		auto stream = C4Compiler::Source::FromMemory("<<=");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::LessLessEquals);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestGreaterGreaterEquals()
	{
		auto stream = C4Compiler::Source::FromMemory(">>=");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::GreaterGreaterEquals);
		TEST_PASSED();
	}

	// --- Assignment compound operators (consolidated into one test) ---

	void CompoundOperatorTest::TestCompoundAssignments()
	{
		// Test a variety of compound assignment operators
		struct TestCase { const char* input; C4Compiler::TokenType expected; };
		TestCase cases[] = {
			{"+=", C4Compiler::TokenType::PlusEquals},
			{"-=", C4Compiler::TokenType::MinusEquals},
			{"*=", C4Compiler::TokenType::StarEquals},
			{"/=", C4Compiler::TokenType::SlashEquals},
			{"%=", C4Compiler::TokenType::PercentEquals},
			{"&=", C4Compiler::TokenType::AmpersandEquals},
			{"|=", C4Compiler::TokenType::PipeEquals},
			{"^=", C4Compiler::TokenType::CaretEquals},
			{"~=", C4Compiler::TokenType::TildeEquals},
			{":=", C4Compiler::TokenType::ColonEquals},
		};

		for (auto& c : cases) {
			auto stream = C4Compiler::Source::FromMemory(c.input);
			C4Compiler::Lexer lexer(*stream, 0);
			auto& t = lexer.NextCompoundToken();
			TEST_ASSERT(t.GetType() == c.expected);
		}
		TEST_PASSED();
	}

	// --- Bitwise and logical compound operators ---

	void CompoundOperatorTest::TestDoubleAmpersandAndDoublePipe()
	{
		auto stream = C4Compiler::Source::FromMemory("&&");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleAmpersand);

		auto stream2 = C4Compiler::Source::FromMemory("||");
		C4Compiler::Lexer lexer2(*stream2, 0);
		auto& t2 = lexer2.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::DoublePipe);
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestPipePipeEquals()
	{
		auto stream = C4Compiler::Source::FromMemory("||=");
		C4Compiler::Lexer lexer(*stream, 0);
		auto& t = lexer.NextCompoundToken();
		TEST_ASSERT(t.GetType() == C4Compiler::TokenType::PipePipeEquals);
		TEST_PASSED();
	}

	// --- Special compound operators ---

	void CompoundOperatorTest::TestDoubleColonDotDotEllipsis()
	{
		// :: scope resolution
		{
			auto stream = C4Compiler::Source::FromMemory("::");
			C4Compiler::Lexer lexer(*stream, 0);
			auto& t = lexer.NextCompoundToken();
			TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleColon);
		}
		// .. range operator
		{
			auto stream = C4Compiler::Source::FromMemory("..");
			C4Compiler::Lexer lexer(*stream, 0);
			auto& t = lexer.NextCompoundToken();
			TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DotDot);
		}
		// ... ellipsis
		{
			auto stream = C4Compiler::Source::FromMemory("...");
			C4Compiler::Lexer lexer(*stream, 0);
			auto& t = lexer.NextCompoundToken();
			TEST_ASSERT(t.GetType() == C4Compiler::TokenType::Ellipsis);
		}
		// @@ double at
		{
			auto stream = C4Compiler::Source::FromMemory("@@");
			C4Compiler::Lexer lexer(*stream, 0);
			auto& t = lexer.NextCompoundToken();
			TEST_ASSERT(t.GetType() == C4Compiler::TokenType::AtAt);
		}
		TEST_PASSED();
	}

	// --- Non-operator passthrough ---

	void CompoundOperatorTest::TestNonOperatorsPassThroughUnchanged()
	{
		// Identifiers, numbers, and strings should pass through unchanged
		auto stream = C4Compiler::Source::FromMemory("x 42 \"hello\"");
		C4Compiler::Lexer lexer(*stream, 0);

		auto& t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Identifier);

		auto& t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Number);

		auto& t3 = lexer.NextCompoundToken();
		TEST_ASSERT(t3.GetType() == C4Compiler::TokenType::Text);
		TEST_PASSED();
	}

	// --- PeekCompound consistency ---

	void CompoundOperatorTest::TestPeekCompoundMatchesNextCompoundToken()
	{
		auto stream = C4Compiler::Source::FromMemory("<<=");
		C4Compiler::Lexer lexer(*stream, 0);

		// Peek should return the same compound token as NextCompoundToken would
		auto& peeked = lexer.PeekCompound();
		TEST_ASSERT(peeked.GetType() == C4Compiler::TokenType::LessLessEquals);

		// Actually consume it
		auto& consumed = lexer.NextCompoundToken();
		TEST_ASSERT(consumed.GetType() == C4Compiler::TokenType::LessLessEquals);
		TEST_PASSED();
	}

	// --- Atomic vs compound token distinction ---

	void CompoundOperatorTest::TestAtomicVsCompoundDistinction()
	{
		// NextToken returns individual atomic tokens even for compound operator sequences.
		// "<<" yields two separate Less tokens via NextToken, but one DoubleLess via NextCompoundToken.
		{
			auto stream = C4Compiler::Source::FromMemory("<<");
			C4Compiler::Lexer lexer(*stream, 0);

			auto& t1 = lexer.NextToken();
			TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Less);
			TEST_ASSERT(t1.GetText() == "<");

			auto& t2 = lexer.NextToken();
			TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Less);
			TEST_ASSERT(t2.GetText() == "<");
		}

		// Same input via NextCompoundToken yields one compound token.
		{
			auto stream2 = C4Compiler::Source::FromMemory("<<");
			C4Compiler::Lexer lexer2(*stream2, 0);

			auto& t = lexer2.NextCompoundToken();
			TEST_ASSERT(t.GetType() == C4Compiler::TokenType::DoubleLess);
			TEST_ASSERT(t.GetText() == "<<");
		}
		TEST_PASSED();
	}

	// --- Mixed sequences with compounds and non-operators ---

	void CompoundOperatorTest::TestMixedCompoundAndNonOperatorSequence()
	{
		// Simulate a realistic expression: "x += y || z"
		// Expected tokens: Identifier("x"), PlusEquals, Identifier("y"), DoublePipe, Identifier("z")
		auto stream = C4Compiler::Source::FromMemory("x += y || z");
		C4Compiler::Lexer lexer(*stream, 0);

		auto& t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Identifier);

		auto& t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::PlusEquals);

		auto& t3 = lexer.NextCompoundToken();
		TEST_ASSERT(t3.GetType() == C4Compiler::TokenType::Identifier);

		auto& t4 = lexer.NextCompoundToken();
		TEST_ASSERT(t4.GetType() == C4Compiler::TokenType::DoublePipe);

		auto& t5 = lexer.NextCompoundToken();
		TEST_ASSERT(t5.GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	// --- PeekCompound at multiple indices ---

	void CompoundOperatorTest::TestPeekCompoundMultipleIndices()
	{
		// Input: "<<= x ||="
		// At index 0: compound <<=(LessLessEquals)
		// At index 1: identifier "x" (passes through unchanged)
		// At index 2: compound ||=(PipePipeEquals)
		auto stream = C4Compiler::Source::FromMemory("<<= x ||=");
		C4Compiler::Lexer lexer(*stream, 0);

		// Peek ahead at three compound tokens without consuming any.
		auto& p0 = lexer.PeekCompound(0);
		TEST_ASSERT(p0.GetType() == C4Compiler::TokenType::LessLessEquals);

		auto& p1 = lexer.PeekCompound(1);
		TEST_ASSERT(p1.GetType() == C4Compiler::TokenType::Identifier);

		auto& p2 = lexer.PeekCompound(2);
		TEST_ASSERT(p2.GetType() == C4Compiler::TokenType::PipePipeEquals);

		// Verify no tokens were consumed - offset should still be 0.
		TEST_ASSERT(lexer.GetOffset() == 0);

		// Now actually consume and verify they match the peeks.
		auto& c0 = lexer.NextCompoundToken();
		TEST_ASSERT(c0.GetType() == C4Compiler::TokenType::LessLessEquals);

		auto& c1 = lexer.NextCompoundToken();
		TEST_ASSERT(c1.GetType() == C4Compiler::TokenType::Identifier);

		auto& c2 = lexer.NextCompoundToken();
		TEST_ASSERT(c2.GetType() == C4Compiler::TokenType::PipePipeEquals);
		TEST_PASSED();
	}

	// --- BUG-119: a compound operator is a run of adjacent characters ---

	namespace
	{
		std::vector<C4Compiler::TokenType> CompoundTypes(const char* text)
		{
			auto stream = C4Compiler::Source::FromMemory(text);
			C4Compiler::Lexer lexer(*stream, 0);
			std::vector<C4Compiler::TokenType> types;
			while (lexer.PeekCompound().GetType() != C4Compiler::TokenType::EndOfFile)
			{
				const C4Compiler::TokenType peeked = lexer.PeekCompound().GetType();
				const C4Compiler::TokenType next = lexer.NextCompoundToken().GetType();
				types.push_back(peeked == next ? next : C4Compiler::TokenType::EndOfFile);
			}
			return types;
		}
	}

	void CompoundOperatorTest::TestWhitespaceEndsACompound()
	{
		using T = C4Compiler::TokenType;
		TEST_ASSERT(CompoundTypes("* ==") == (std::vector<T>{ T::Star, T::DoubleEqual }));
		TEST_ASSERT(CompoundTypes("< =") == (std::vector<T>{ T::Less, T::Equal }));
		TEST_ASSERT(CompoundTypes("+ +") == (std::vector<T>{ T::Plus, T::Plus }));
		TEST_ASSERT(CompoundTypes("<<\t=") == (std::vector<T>{ T::DoubleLess, T::Equal }));
		TEST_ASSERT(CompoundTypes("-\n>") == (std::vector<T>{ T::Minus, T::Greater }));
		TEST_ASSERT(CompoundTypes("*==") == (std::vector<T>{ T::StarEquals, T::Equal }));
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestCommentEndsACompound()
	{
		using T = C4Compiler::TokenType;
		TEST_ASSERT(CompoundTypes("</**/=") == (std::vector<T>{ T::Less, T::Equal }));
		TEST_ASSERT(CompoundTypes("&&/**/&") == (std::vector<T>{ T::DoubleAmpersand, T::Ampersand }));
		TEST_PASSED();
	}

	void CompoundOperatorTest::TestCompoundSpansItsCharacters()
	{
		auto stream = C4Compiler::Source::FromMemory("x <<= y");
		C4Compiler::Lexer lexer(*stream, 0);
		lexer.NextCompoundToken();
		const C4Compiler::Token& op = lexer.NextCompoundToken();
		TEST_ASSERT(op.GetType() == C4Compiler::TokenType::LessLessEquals);
		TEST_ASSERT_EQ((size_t)op.GetByteOffset(), (size_t)2);
		TEST_ASSERT_EQ((size_t)op.GetStartLocation().byteOffset, (size_t)2);
		TEST_ASSERT_EQ((size_t)op.GetEndLocation().byteOffset, (size_t)5);
		TEST_PASSED();
	}
}
