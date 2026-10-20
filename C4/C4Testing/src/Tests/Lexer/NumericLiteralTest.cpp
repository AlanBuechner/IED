#include "NumericLiteralTest.h"
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
		std::vector<TokenType> Types(const char* source)
		{
			auto stream = Source::FromMemory(source);
			Lexer lexer(*stream, 0);
			std::vector<TokenType> types;
			while (lexer.PeekCompound().GetType() != TokenType::EndOfFile)
				types.push_back(lexer.NextCompoundToken().GetType());
			return types;
		}

		bool Spelled(const char* text, uint32 radix, bool isFloat, const char* digits, const char* suffix)
		{
			const NumberSpelling s = SplitNumber(text);
			return s.radix == radix && s.isFloat == isFloat && s.digits == digits && s.suffix == suffix &&
				s.length == std::string(text).size();
		}

		LiteralFacts FactsOf(const char* text) { return ScanNumber(text).facts; }
	}

	void NumericLiteralTest::SpellingSplitsRadixDigitsAndSuffix()
	{
		TEST_ASSERT(Spelled("42", 10, false, "42", ""));
		TEST_ASSERT(Spelled("0", 10, false, "0", ""));
		TEST_ASSERT(Spelled("017", 8, false, "017", ""));
		TEST_ASSERT(Spelled("0x1Fu", 16, false, "1F", "u"));
		TEST_ASSERT(Spelled("0XffULL", 16, false, "ff", "ULL"));
		TEST_ASSERT(Spelled("0b101l", 2, false, "101", "l"));
		TEST_ASSERT(Spelled("0.5", 10, true, "0.5", ""));
		TEST_ASSERT(Spelled("09.5", 10, true, "09.5", ""));
		TEST_ASSERT(Spelled("1e+5", 10, true, "1e+5", ""));
		TEST_ASSERT(Spelled("2.5e-3f", 10, true, "2.5e-3", "f"));
		TEST_ASSERT(Spelled("12abc", 10, false, "12", "abc"));
	}

	void NumericLiteralTest::OperatorsAfterANumberStayOperators()
	{
		using T = TokenType;
		TEST_ASSERT(Types("5-3") == (std::vector<T>{ T::Number, T::Minus, T::Number }));
		TEST_ASSERT(Types("4+2") == (std::vector<T>{ T::Number, T::Plus, T::Number }));
		TEST_ASSERT(Types("2...5") == (std::vector<T>{ T::Number, T::Ellipsis, T::Number }));
		TEST_ASSERT(Types("a[1...3]") == (std::vector<T>{ T::Identifier, T::OpenBracket, T::Number, T::Ellipsis, T::Number, T::CloseBracket }));
		TEST_ASSERT(Types("0..5") == (std::vector<T>{ T::Number, T::DotDot, T::Number }));
		TEST_ASSERT(Types("1.x") == (std::vector<T>{ T::Number, T::Dot, T::Identifier }));
		TEST_ASSERT(Types("1e+x") == (std::vector<T>{ T::Number, T::Plus, T::Identifier }));
		TEST_ASSERT(Types("a[0]") == (std::vector<T>{ T::Identifier, T::OpenBracket, T::Number, T::CloseBracket }));
	}

	void NumericLiteralTest::EveryValidSuffixOnEveryRadix()
	{
		const char* radixes[] = { "10", "0x1F", "0b10", "017" };
		const char* suffixes[] = { "", "u", "U", "l", "L", "ll", "LL", "ul", "uL", "Ul", "lu", "LU", "ull", "uLL", "ULL", "llu", "LLU" };
		for (const char* number : radixes)
			for (const char* suffix : suffixes)
			{
				const std::string text = std::string(number) + suffix;
				TEST_ASSERT_MSG(FactsOf(text.c_str()).issue == LiteralIssue::None, "{0} must be valid", text.c_str());
			}
		const char* floats[] = { "1.5", "1.5f", "1.5F", "1e3", "1e3f", "2.5E-3F" };
		for (const char* text : floats)
			TEST_ASSERT_MSG(FactsOf(text).issue == LiteralIssue::None, "{0} must be valid", text);
	}

	void NumericLiteralTest::InvalidSpellingsHaveOneIssue()
	{
		const struct { const char* text; LiteralIssue issue; uint32 detail; } cases[] = {
			{ "10q", LiteralIssue::InvalidSuffix, 2 },
			{ "12abc", LiteralIssue::InvalidSuffix, 2 },
			{ "10uu", LiteralIssue::InvalidSuffix, 2 },
			{ "10lL", LiteralIssue::InvalidSuffix, 2 },
			{ "10lul", LiteralIssue::InvalidSuffix, 2 },
			{ "1.5u", LiteralIssue::InvalidSuffix, 3 },
			{ "1.5ff", LiteralIssue::InvalidSuffix, 3 },
			{ "1f", LiteralIssue::FloatSuffixOnInteger, 0 },
			{ "10F", LiteralIssue::FloatSuffixOnInteger, 0 },
			{ ".5", LiteralIssue::NoLeadingDigit, 0 },
			{ ".0f", LiteralIssue::NoLeadingDigit, 0 },
			{ ".5e3", LiteralIssue::NoLeadingDigit, 0 },
			{ "0x", LiteralIssue::MissingDigits, 'x' },
			{ "0bu", LiteralIssue::MissingDigits, 'b' },
			{ "1e", LiteralIssue::MissingDigits, 'e' },
			{ "0b12", LiteralIssue::InvalidDigit, '2' },
			{ "09", LiteralIssue::InvalidDigit, '9' },
			{ "0128", LiteralIssue::InvalidDigit, '8' },
		};
		for (const auto& c : cases)
		{
			const ScannedLiteral literal = ScanNumber(c.text);
			TEST_ASSERT_MSG(literal.facts.issue == c.issue && literal.facts.detail == c.detail, "{0}", c.text);
			TEST_ASSERT_MSG(literal.length == std::string(c.text).size(), "{0} is one token", c.text);
		}
	}

	void NumericLiteralTest::IssuesAreWordedAndReportedOnce()
	{
		auto stream = Source::FromMemory("10q 1f 0b12 7 v.5f");
		Lexer lexer(*stream, 0);
		std::vector<std::string> messages;
		while (lexer.PeekCompound().GetType() != TokenType::EndOfFile)
		{
			const Token& token = lexer.NextCompoundToken();
			if (token.GetLiteralFacts().issue != LiteralIssue::None)
				messages.push_back(LiteralIssueMessage(token));
		}
		TEST_ASSERT(messages.size() == 4);
		TEST_ASSERT(messages[0].find("'10q' has the invalid suffix 'q'") == 0);
		TEST_ASSERT(messages[1] == "'1f' is an integer: a float literal needs a '.' or an exponent, write 1.0f");
		TEST_ASSERT(messages[2] == "'0b12' is binary and cannot hold the digit '2'");
		TEST_ASSERT(messages[3] == "'.5f' needs a digit before the '.': write 0.5f");
		TEST_ASSERT(lexer.Diagnostics().size() == 4);
		TEST_ASSERT(lexer.Diagnostics().begin()->second.location.column == 1);
	}
} // namespace Testing
