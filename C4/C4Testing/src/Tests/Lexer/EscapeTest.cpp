#include "EscapeTest.h"
#include "Lexer/LiteralScanner.h"
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

		std::vector<uint32> Units(const char* source)
		{
			const ScannedLiteral literal = Scan(source);
			std::vector<uint32> units;
			for (size_t i = 0; i < CodeUnitCount(literal.text, literal.facts.unitWidth); ++i)
				units.push_back(CodeUnitAt(literal.text, literal.facts.unitWidth, i));
			return units;
		}

		bool Clean(const char* source) { return Scan(source).facts.issue == LiteralIssue::None; }

		bool Fails(const char* source, LiteralIssue issue, uint32 detail)
		{
			const LiteralFacts facts = Scan(source).facts;
			return facts.issue == issue && facts.detail == detail;
		}
	}

	void EscapeTest::SimpleEscapes()
	{
		using U = std::vector<uint32>;
		TEST_ASSERT(Units(R"("\n\t\r\a\b\f\v\e")") == (U{ 10, 9, 13, 7, 8, 12, 11, 27 }));
		TEST_ASSERT(Units(R"("\\\"\'\?\0")") == (U{ '\\', '"', '\'', '?', 0 }));
		TEST_ASSERT(Units(R"('\'')") == (U{ '\'' }));
		TEST_ASSERT(Clean(R"("\n\t\r\a\b\f\v\e\\\"\'\?\0")"));
	}

	void EscapeTest::HexEscapesTakeTwoDigitsPerByte()
	{
		using U = std::vector<uint32>;
		TEST_ASSERT(Units(R"("\x41")") == (U{ 'A' }));
		TEST_ASSERT(Units(R"("\x41B")") == (U{ 'A', 'B' }));
		TEST_ASSERT(Units(R"("\x1ff")") == (U{ 0x1F, 'f' }));
		TEST_ASSERT(Units(R"("\x41Z")") == (U{ 'A', 'Z' }));
		TEST_ASSERT(Units(R"("\xFF")") == (U{ 0xFF }));
		TEST_ASSERT(Units(R"(u"\x1234")") == (U{ 0x1234 }));
		TEST_ASSERT(Units(R"(u"\x12345")") == (U{ 0x1234, '5' }));
		TEST_ASSERT(Units(R"(U"\x0001F600")") == (U{ 0x1F600 }));
		TEST_ASSERT(Clean(R"('\xFF')"));
	}

	void EscapeTest::OctalEscapes()
	{
		using U = std::vector<uint32>;
		TEST_ASSERT(Units(R"("\101")") == (U{ 'A' }));
		TEST_ASSERT(Units(R"("\0")") == (U{ 0 }));
		TEST_ASSERT(Units(R"("\1012")") == (U{ 'A', '2' }));
		TEST_ASSERT(Units(R"("\377")") == (U{ 0xFF }));
		TEST_ASSERT(Units(R"(u"\777")") == (U{ 0777 }));
		TEST_ASSERT(Fails(R"("\777")", LiteralIssue::EscapeOutOfRange, 0777));
	}

	void EscapeTest::UniversalEscapesEncodeInTheLiteralsWidth()
	{
		using U = std::vector<uint32>;
		TEST_ASSERT(Units(R"("\u00e9")") == (U{ 0xC3, 0xA9 }));                 // UTF-8
		TEST_ASSERT(Units(R"("\u20AC")") == (U{ 0xE2, 0x82, 0xAC }));
		TEST_ASSERT(Units(R"(u"\u00e9")") == (U{ 0xE9 }));                     // UTF-16
		TEST_ASSERT(Units(R"(u"\U0001F600")") == (U{ 0xD83D, 0xDE00 }));       // a surrogate pair
		TEST_ASSERT(Units(R"(U"\U0001F600")") == (U{ 0x1F600 }));              // UTF-32
		TEST_ASSERT(Units(R"(u8"\u00e9")") == (U{ 0xC3, 0xA9 }));
		TEST_ASSERT(Clean(R"(u'\u00e9')"));
		TEST_ASSERT(Clean(R"(U'\U0001F600')"));
	}

	void EscapeTest::BadEscapesAreErrors()
	{
		TEST_ASSERT(Fails(R"("\q")", LiteralIssue::UnknownEscape, 'q'));
		TEST_ASSERT(Fails(R"("\8")", LiteralIssue::UnknownEscape, '8'));
		TEST_ASSERT(Fails(R"("\x")", LiteralIssue::UnknownEscape, 'x'));
		TEST_ASSERT(Fails(R"("\xG")", LiteralIssue::UnknownEscape, 'x'));
		TEST_ASSERT(Fails(R"("\u12")", LiteralIssue::BadUniversalEscape, 'u'));
		TEST_ASSERT(Fails(R"("\U0001F60")", LiteralIssue::BadUniversalEscape, 'U'));
		TEST_ASSERT(Fails(R"("\uD800")", LiteralIssue::BadUniversalEscape, 'u'));
		TEST_ASSERT(Fails(R"("\U00110000")", LiteralIssue::BadUniversalEscape, 'U'));
		// The first issue wins; the literal still ends at its closing quote.
		const ScannedLiteral both = Scan(R"("\q\z" x)");
		TEST_ASSERT(both.facts.detail == 'q' && both.length == 6);
	}
} // namespace Testing
