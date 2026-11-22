#include "BraceRecoveryTest.h"
#include "Lexer/BraceRecovery.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include <string>
#include <vector>

namespace Testing
{
	namespace
	{
		using namespace C4Compiler;

		// A file's tokens as brace recovery reads them, each with its line, so a test names a repair by lines.
		struct Facts
		{
			std::vector<BraceFact> facts;
			std::vector<uint32> lines;
		};

		Facts FactsOf(const char* text)
		{
			Ref<Source> source = Source::FromMemory(text);
			Lexer lexer(*source, 1);
			Facts out;
			uint32 line = 0, indent = 0;
			for (;;)
			{
				const Token token = lexer.NextToken();
				if (token.GetType() == TokenType::Comment || token.GetType() == TokenType::MultiComment) continue;
				const bool startsLine = token.GetStartLocation().line != line;
				if (startsLine)
				{
					line = token.GetStartLocation().line;
					indent = token.GetStartLocation().column;
				}
				out.facts.push_back({ token.GetType(), indent, startsLine });
				out.lines.push_back(token.GetStartLocation().line);
				if (token.GetType() == TokenType::EndOfFile) break;
			}
			return out;
		}

		// Each closing as (line of its `{`, line of the token it goes before); the end of the file's line is 0.
		std::vector<std::pair<uint32, uint32>> ClosingLines(const Facts& facts, const BraceRepairs& repairs)
		{
			std::vector<std::pair<uint32, uint32>> lines;
			for (const BraceRepairs::Closing& closing : repairs.closings)
			{
				const bool atEnd = facts.facts[closing.before].type == TokenType::EndOfFile;
				lines.push_back({ facts.lines[closing.open], atEnd ? 0 : facts.lines[closing.before] });
			}
			return lines;
		}

		using Lines = std::vector<std::pair<uint32, uint32>>;
	}

	void BraceRecoveryTest::PairedBracesAreLeftAlone()
	{
		const Facts facts = FactsOf("struct A\n{\nint32 x;\n}\nint32 F() {\n\treturn 1;\n}\n");
		TEST_ASSERT(PlanBraceRepairs(facts.facts).Empty());
		TEST_PASSED();
	}

	void BraceRecoveryTest::EmptyFileHasNothingToMend()
	{
		TEST_ASSERT(PlanBraceRepairs(FactsOf("").facts).Empty());
		TEST_ASSERT(PlanBraceRepairs({}).Empty());
		TEST_PASSED();
	}

	void BraceRecoveryTest::OpenBlockAtEndClosesBeforeOutdentedLine()
	{
		const Facts facts = FactsOf(
			"struct Point\n"
			"{\n"
			"\tpublic int32 x;\n"
			"\tpublic int32 Get()\n"
			"\t{\n"
			"\t\treturn x;\n"
			"\t}\n"
			"int32 Twice(int32 v) { return v * 2; }\n");
		const BraceRepairs repairs = PlanBraceRepairs(facts.facts);
		TEST_ASSERT(ClosingLines(facts, repairs) == Lines({ { 2, 8 } }));
		TEST_ASSERT(repairs.dropped.empty());
		TEST_PASSED();
	}

	void BraceRecoveryTest::OpenBlockWithNothingAfterClosesAtEnd()
	{
		const Facts facts = FactsOf("void F()\n{\n\tint32 x = 1;\n");
		TEST_ASSERT(ClosingLines(facts, PlanBraceRepairs(facts.facts)) == Lines({ { 2, 0 } }));
		TEST_PASSED();
	}

	void BraceRecoveryTest::LessIndentedCloserClosesInnerBlockFirst()
	{
		// K&R braces: the `if` block is never closed; the `}` of line 4 is the function's.
		const Facts facts = FactsOf(
			"void F() {\n"
			"\tif (x) {\n"
			"\t\treturn;\n"
			"}\n"
			"void G() {}\n");
		TEST_ASSERT(ClosingLines(facts, PlanBraceRepairs(facts.facts)) == Lines({ { 2, 4 } }));
		TEST_PASSED();
	}

	void BraceRecoveryTest::SameLineBracesAreNotOutdents()
	{
		// `{ return x; }` on one line pairs up by itself; only the type's `{` is open.
		const Facts facts = FactsOf(
			"\tstruct A\n"
			"\t{\n"
			"\t\tint32 Get() { return x; }\n"
			"int32 y;\n");
		TEST_ASSERT(ClosingLines(facts, PlanBraceRepairs(facts.facts)) == Lines({ { 2, 4 } }));
		TEST_PASSED();
	}

	void BraceRecoveryTest::CommentAtColumnOneIsNoOutdent()
	{
		const Facts facts = FactsOf(
			"void F()\n"
			"{\n"
			"// commented out\n"
			"\tint32 x = 1;\n"
			"void G() {}\n");
		TEST_ASSERT(ClosingLines(facts, PlanBraceRepairs(facts.facts)) == Lines({ { 2, 5 } }));
		TEST_PASSED();
	}

	void BraceRecoveryTest::DeeperIndentedCloserIsDropped()
	{
		const Facts facts = FactsOf(
			"int32 Clamp(int32 x)\n"
			"{\n"
			"\tif (x > 10)\n"
			"\t\treturn 10;\n"
			"\t}\n"
			"\treturn x;\n"
			"}\n");
		const BraceRepairs repairs = PlanBraceRepairs(facts.facts);
		TEST_ASSERT(repairs.closings.empty());
		TEST_ASSERT_EQ(repairs.dropped.size(), size_t(1));
		TEST_ASSERT_EQ(facts.lines[repairs.dropped[0]], uint32(5));
		TEST_PASSED();
	}

	void BraceRecoveryTest::StrayCloserAtRootIsLeftToTheParser()
	{
		const Facts facts = FactsOf("}\nint32 F() { return 1; }\n");
		TEST_ASSERT(PlanBraceRepairs(facts.facts).Empty());
		TEST_PASSED();
	}

	void BraceRecoveryTest::LexerInsertsTheClosingBrace()
	{
		Ref<Source> source = Source::FromMemory("struct A\n{\n\tint32 x;\nint32 y;\n");
		Lexer lexer(*source, 1);
		const std::vector<LexicalDiagnostic> errors = lexer.RecoverBraces();
		TEST_ASSERT_EQ(errors.size(), size_t(1));
		TEST_ASSERT_EQ(errors[0].location.line, uint32(2));
		TEST_ASSERT_EQ(errors[0].location.column, uint32(1));
		std::vector<std::string> texts;
		for (Token token = lexer.NextCompoundToken(); token.GetType() != TokenType::EndOfFile; token = lexer.NextCompoundToken())
			texts.push_back(token.GetText().c_str());
		TEST_ASSERT(texts == std::vector<std::string>({ "struct", "A", "{", "int32", "x", ";", "}", "int32", "y", ";" }));
		TEST_PASSED();
	}
}
