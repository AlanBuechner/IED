#include "QuotedLiteralTest.h"
#include "Lexer/Lexer.h"
#include "Lexer/LiteralScanner.h"
#include "SourceManager/Source.h"
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		ScannedLiteral Scan(const char* source)
		{
			const std::string_view text(source);
			return ScanQuoted(text, QuotedLiteralPrefix(text));
		}

		std::vector<uint32> Units(const ScannedLiteral& literal)
		{
			std::vector<uint32> units;
			for (size_t i = 0; i < CodeUnitCount(literal.text, literal.facts.unitWidth); ++i)
				units.push_back(CodeUnitAt(literal.text, literal.facts.unitWidth, i));
			return units;
		}

		bool Is(const char* source, TokenType type, uint8 width, std::vector<uint32> units)
		{
			const ScannedLiteral literal = Scan(source);
			return literal.type == type && literal.facts.unitWidth == width && Units(literal) == units &&
				literal.facts.issue == LiteralIssue::None && literal.length == std::string(source).size();
		}

		LiteralFacts FactsOf(const char* source) { return Scan(source).facts; }
	}

	void QuotedLiteralTest::PrefixesPickTheWidth()
	{
		TEST_ASSERT(Is(R"("ab")", TokenType::Text, 1, { 'a', 'b' }));
		TEST_ASSERT(Is(R"(u8"ab")", TokenType::Text, 1, { 'a', 'b' }));
		TEST_ASSERT(Is(R"(u"ab")", TokenType::Text, 2, { 'a', 'b' }));
		TEST_ASSERT(Is(R"(U"ab")", TokenType::Text, 4, { 'a', 'b' }));
		TEST_ASSERT(Is(R"("")", TokenType::Text, 1, {}));
		TEST_ASSERT(Is(R"('a')", TokenType::Char, 1, { 'a' }));
		TEST_ASSERT(Is(R"(u8'a')", TokenType::Char, 1, { 'a' }));
		TEST_ASSERT(Is(R"(u'a')", TokenType::Char, 2, { 'a' }));
		TEST_ASSERT(Is(R"(U'a')", TokenType::Char, 4, { 'a' }));
	}

	void QuotedLiteralTest::NamesStartingWithUStayNames()
	{
		auto stream = Source::FromMemory(R"(u8x u uu U8"s" u8 "t")");
		Lexer lexer(*stream, 0);
		const TokenType expected[] = { TokenType::Identifier, TokenType::Identifier, TokenType::Identifier,
			TokenType::Identifier, TokenType::Text, TokenType::Identifier, TokenType::Text };
		for (TokenType type : expected)
			TEST_ASSERT(lexer.NextCompoundToken().GetType() == type);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == TokenType::EndOfFile);
	}

	void QuotedLiteralTest::CharacterLiteralsHoldOneCharacter()
	{
		TEST_ASSERT(FactsOf(R"('')").issue == LiteralIssue::EmptyCharacter);
		TEST_ASSERT(FactsOf(R"('ab')").issue == LiteralIssue::MultiCharacter);
		TEST_ASSERT(FactsOf(R"('\n\t')").issue == LiteralIssue::MultiCharacter);
		const LiteralFacts e = FactsOf("'\xC3\xA9'"); // e-acute as UTF-8: two char8 units
		TEST_ASSERT(e.issue == LiteralIssue::CharacterOutOfRange && e.detail == 0xE9);
		TEST_ASSERT(Is("u'\xC3\xA9'", TokenType::Char, 2, { 0xE9 }));
		const LiteralFacts emoji = FactsOf(R"(u'\U0001F600')");
		TEST_ASSERT(emoji.issue == LiteralIssue::CharacterOutOfRange && emoji.detail == 0x1F600);
		TEST_ASSERT(Is(R"(U'\U0001F600')", TokenType::Char, 4, { 0x1F600 }));
		TEST_ASSERT(Is(R"('\xFF')", TokenType::Char, 1, { 0xFF }));
		TEST_ASSERT(Is(R"('"')", TokenType::Char, 1, { '"' }));
	}

	void QuotedLiteralTest::SourceUtf8IsReencodedForWideLiterals()
	{
		TEST_ASSERT(Is("\"\xC3\xA9\"", TokenType::Text, 1, { 0xC3, 0xA9 }));                 // char8 keeps the bytes
		TEST_ASSERT(Is("u\"\xC3\xA9\xE2\x82\xAC\"", TokenType::Text, 2, { 0xE9, 0x20AC }));
		TEST_ASSERT(Is("u\"\xF0\x9F\x98\x80\"", TokenType::Text, 2, { 0xD83D, 0xDE00 }));
		TEST_ASSERT(Is("U\"\xF0\x9F\x98\x80\"", TokenType::Text, 4, { 0x1F600 }));
		TEST_ASSERT(FactsOf("u\"\xFF\"").issue == LiteralIssue::InvalidUtf8);
		TEST_ASSERT(FactsOf("U\"\xC3\"").issue == LiteralIssue::InvalidUtf8);
		TEST_ASSERT(FactsOf("u\"\xED\xA0\x80\"").issue == LiteralIssue::InvalidUtf8); // an encoded surrogate
	}

	void QuotedLiteralTest::LiteralsEndOnTheirLine()
	{
		const ScannedLiteral open = Scan("\"abc\nx\"");
		TEST_ASSERT(open.facts.issue == LiteralIssue::Unterminated && open.facts.detail == '"' && open.length == 4);
		const ScannedLiteral character = Scan("'a");
		TEST_ASSERT(character.facts.issue == LiteralIssue::Unterminated && character.facts.detail == '\'');
		const ScannedLiteral continued = Scan("\"ab\\\ncd\"");
		TEST_ASSERT(continued.facts.issue == LiteralIssue::UnknownEscape && continued.facts.detail == '\n');

		// The lexer reports an issue once, at the literal, and keeps lexing after it.
		auto stream = Source::FromMemory("\"a\\q\" 'xy' 7");
		Lexer lexer(*stream, 0);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == TokenType::Text);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == TokenType::Char);
		TEST_ASSERT(lexer.NextCompoundToken().GetType() == TokenType::Number);
		TEST_ASSERT(lexer.Diagnostics().size() == 2);
		TEST_ASSERT(lexer.Diagnostics().begin()->second.message == "unknown escape sequence '\\q'");
	}
} // namespace Testing
