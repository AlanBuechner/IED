#include "MacroLayoutTest.h"
#include "Macro/MacroPreprocessor.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include <algorithm>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		Ref<MacroLayout> LayoutOf(const std::string& text)
		{
			MacroTable table;
			return MacroPreprocessor::Process(text, "t.cf", table, nullptr).layout;
		}

		uint32 At(const std::string& text, const std::string& what, size_t from = 0)
		{
			return static_cast<uint32>(text.find(what, from));
		}

		// Every token the lexer reads of `text` through its layout, up to the end of the file.
		std::vector<Token> Tokens(const std::string& text)
		{
			Ref<MacroLayout> layout = LayoutOf(text);
			Ref<Source> source = Source::FromMemory(text);
			Lexer lexer(*source, 1, layout.Get());
			std::vector<Token> tokens;
			for (Token token = lexer.NextCompoundToken(); token.GetType() != TokenType::EndOfFile; token = lexer.NextCompoundToken())
				tokens.push_back(token);
			return tokens;
		}

		const Token* Find(const std::vector<Token>& tokens, const std::string& text, size_t nth = 0)
		{
			for (const Token& token : tokens)
				if (token.GetText() == text.c_str() && nth-- == 0) return &token;
			return nullptr;
		}
	}

	void MacroLayoutTest::UsesAreExpansionsOverTheFilesText()
	{
		const std::string text = "#define TWO 2\n#define ADD(a, b) ((a) + (b))\nint32 x = TWO + ADD(1, TWO);\n";
		const Ref<MacroLayout> layout = LayoutOf(text);
		TEST_ASSERT_EQ(layout->ExpansionCount(), uint32(2));
		const MacroExpansion& two = layout->Expansion(1);
		TEST_ASSERT(two.macro == "TWO" && two.text == "2");
		TEST_ASSERT_EQ(two.begin, At(text, "TWO +"));
		TEST_ASSERT_EQ(two.end, At(text, "TWO +") + 3);
		const MacroExpansion& add = layout->Expansion(2);
		TEST_ASSERT(add.macro == "ADD" && add.text == "((1) + (2))");
		TEST_ASSERT_EQ(add.begin, At(text, "ADD(1"));
		TEST_ASSERT_EQ(add.end, At(text, ";\n", add.begin));
		TEST_ASSERT_EQ(layout->ExpansionAt(At(text, "TWO)")), uint32(2)); // inside the call
		TEST_ASSERT_EQ(layout->ExpansionAt(At(text, "int32")), uint32(0));
		TEST_PASSED();
	}

	void MacroLayoutTest::DirectivesAndInactiveGroupsAreLeftOut()
	{
		const std::string text = "#if 0\nhidden\n#else\nshown\n#endif\n";
		const Ref<MacroLayout> layout = LayoutOf(text);
		TEST_ASSERT_EQ(layout->Regions().size(), size_t(2));
		TEST_ASSERT_EQ(layout->Regions()[0].begin, uint32(0));
		TEST_ASSERT_EQ(layout->Regions()[0].end, At(text, "shown")); // the #if line, the inactive line and #else: one part
		TEST_ASSERT_EQ(layout->Regions()[1].begin, At(text, "#endif"));
		TEST_ASSERT(layout->Render(text, 0, static_cast<uint32>(text.size())) == "\n\n\nshown\n\n");
		TEST_PASSED();
	}

	void MacroLayoutTest::ArgumentTokensKeepTheirPlace()
	{
		const std::string text = "#define TWICE(x) (x) * 2\nint32 y = TWICE(count);\n";
		const Ref<MacroLayout> layout = LayoutOf(text); // an expansion is the layout's: it lives as long as the layout does
		const MacroExpansion& twice = layout->Expansion(1);
		TEST_ASSERT(twice.text == "(count) * 2");
		TEST_ASSERT_EQ(twice.pieces.size(), size_t(5));
		TEST_ASSERT_EQ(twice.pieces[0].fileOffset, MacroToken::kNotInFile); // the body's `(`
		TEST_ASSERT_EQ(twice.pieces[1].fileOffset, At(text, "count"));      // the argument, where the file has it
		TEST_ASSERT_EQ(twice.pieces[1].textBegin, uint32(1));
		TEST_ASSERT_EQ(twice.pieces[4].fileOffset, MacroToken::kNotInFile);
		TEST_ASSERT(twice.PieceAt(3) == &twice.pieces[1]);
		TEST_ASSERT(twice.PieceAt(7) == nullptr); // the space before `*`
		TEST_PASSED();
	}

	void MacroLayoutTest::RenderKeepsEveryLine()
	{
		const std::string text = "#define ADD(a, b) a+b\nint32 z = ADD(1,\n2); int32 w;\n";
		const std::string rendered = LayoutOf(text)->Render(text, 0, static_cast<uint32>(text.size()));
		TEST_ASSERT(rendered == "\nint32 z = 1+2\n; int32 w;\n");
		TEST_ASSERT_EQ(std::count(rendered.begin(), rendered.end(), '\n'), std::count(text.begin(), text.end(), '\n'));
		const std::string part = LayoutOf(text)->Render(text, At(text, "int32 z"), At(text, "int32 w"));
		TEST_ASSERT(part == "int32 z = 1+2\n; ");
		TEST_PASSED();
	}

	void MacroLayoutTest::RenderedOffsetsFollowTheExpansions()
	{
		const std::string text = "#define ADD(a, b) a+b\nint32 z = ADD(1,\n2); int32 w;\n";
		const Ref<MacroLayout> layout = LayoutOf(text);
		const std::string rendered = layout->Render(text, 0, static_cast<uint32>(text.size()));
		TEST_ASSERT_EQ(layout->RenderedOffset(text, At(text, "int32 w"), 0, 0), uint32(rendered.find("int32 w")));
		TEST_ASSERT_EQ(layout->RenderedOffset(text, At(text, "int32 z"), 0, 0), uint32(rendered.find("int32 z")));
		TEST_ASSERT_EQ(layout->RenderedOffset(text, At(text, "ADD("), 1, 2), uint32(rendered.find("1+2") + 2)); // the `2` of `1+2`
		TEST_ASSERT_EQ(layout->RenderedOffset(text, 3, 0, 0), uint32(0)); // inside the directive left out: where it was
		TEST_PASSED();
	}

	void MacroLayoutTest::UnchangedUseIsNoExpansion()
	{
		TEST_ASSERT_EQ(LayoutOf("#define SELF SELF\nSELF;\n")->ExpansionCount(), uint32(0));
		TEST_ASSERT_EQ(LayoutOf("#define F(x) x\nint32 F;\n")->ExpansionCount(), uint32(0)); // a function-like name not called
		TEST_ASSERT(LayoutOf("int32 x;\n")->Empty());
		TEST_PASSED();
	}

	void MacroLayoutTest::LexerLocatesTokensOfAnExpansion()
	{
		const std::string text = "#define TWICE(x) (x) * 2\nint32 y = TWICE(count) + 1;\n";
		const std::vector<Token> tokens = Tokens(text);
		const Token* open = Find(tokens, "(");
		TEST_ASSERT(open != nullptr);
		TEST_ASSERT_EQ(open->GetStartLocation().line, uint32(2));
		TEST_ASSERT_EQ(open->GetStartLocation().column, uint32(11)); // a body's token is at its use
		TEST_ASSERT_EQ(open->GetStartLocation().expansion, uint32(1));
		TEST_ASSERT_EQ(open->GetEndLocation().column, uint32(23));   // and ends where the use does
		const Token* count = Find(tokens, "count");
		TEST_ASSERT(count != nullptr);
		TEST_ASSERT_EQ(count->GetStartLocation().column, uint32(17)); // an argument's at its own place
		TEST_ASSERT_EQ(count->GetEndLocation().column, uint32(22));
		TEST_ASSERT_EQ(count->GetStartLocation().expansion, uint32(1));
		TEST_ASSERT_EQ(count->GetStartLocation().inExpansion, uint32(1));
		TEST_ASSERT(SourceLocation::Before(open->GetStartLocation(), Find(tokens, "*")->GetStartLocation()));
		const Token* plus = Find(tokens, "+");
		TEST_ASSERT(plus != nullptr);
		TEST_ASSERT_EQ(plus->GetStartLocation().column, uint32(24));
		TEST_ASSERT_EQ(plus->GetStartLocation().expansion, uint32(0));
		TEST_PASSED();
	}

	void MacroLayoutTest::LexerKeepsTheColumnsAfterAUse()
	{
		// BUG-446's case, now by construction: no text is rewritten, so what follows a use is where the file has it.
		const std::string text = "#define LONG_MACRO_NAME 1\nint32 a = LONG_MACRO_NAME + LONG_MACRO_NAME; int64 b = 2;\n";
		const std::vector<Token> tokens = Tokens(text);
		const Token* int64Token = Find(tokens, "int64");
		TEST_ASSERT(int64Token != nullptr);
		const uint32 lineStart = At(text, "int32 a");
		TEST_ASSERT_EQ(int64Token->GetStartLocation().column, At(text, "int64") - lineStart + 1);
		TEST_ASSERT_EQ(int64Token->GetStartLocation().byteOffset, At(text, "int64"));
		TEST_PASSED();
	}

	void MacroLayoutTest::LexerJoinsOperatorsOnlyWithinAnExpansion()
	{
		const std::vector<Token> shift = Tokens("#define SHL <<\nint32 a = 1 SHL 2;\n");
		TEST_ASSERT(Find(shift, "<<") != nullptr);
		const std::vector<Token> apart = Tokens("#define LT <\nint32 b = 1 LT < 2;\n");
		TEST_ASSERT(Find(apart, "<<") == nullptr && Find(apart, "<", 1) != nullptr); // white space at the use's end
		const std::vector<Token> joined = Tokens("#define LT <\nint32 c = 1 LT<2;\n");
		TEST_ASSERT(Find(joined, "<<") != nullptr); // as the text had it: `LT<` read `<<`
		TEST_PASSED();
	}

	void MacroLayoutTest::LexerPassesOverLeftOutLines()
	{
		const std::vector<Token> tokens = Tokens("#if 0\nint32 hidden;\n#endif\nint32 shown;\n");
		TEST_ASSERT_EQ(tokens.size(), size_t(3));
		TEST_ASSERT(tokens[1].GetText() == "shown");
		TEST_ASSERT_EQ(tokens[1].GetStartLocation().line, uint32(4));
		TEST_PASSED();
	}

	void MacroLayoutTest::ExpansionKnowsWhereItsMacroIsDefined()
	{
		const Ref<MacroLayout> layout = LayoutOf("int32 a;\n#define TWO 2\nint32 b = TWO;\nint32 c = __LINE__;\n");
		TEST_ASSERT_EQ(layout->ExpansionCount(), uint32(2));
		TEST_ASSERT(layout->Expansion(1).definedIn == "t.cf");
		TEST_ASSERT_EQ(layout->Expansion(1).definedLine, uint32(2));
		TEST_ASSERT(layout->Expansion(2).definedIn.empty() && layout->Expansion(2).text == "4"); // __LINE__ has no #define
		TEST_PASSED();
	}
}
