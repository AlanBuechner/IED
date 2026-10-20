#include "Lexer/LiteralScanner.h"
#include <cctype>
#include <string>

namespace Scripting
{
	namespace
	{
		bool IsDecimalDigit(char c) { return c >= '0' && c <= '9'; }
		bool IsHexDigit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
		bool IsSuffixCharacter(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

		size_t SkipWhile(std::string_view text, size_t i, bool (*keep)(char))
		{
			while (i < text.size() && keep(text[i])) ++i;
			return i;
		}

		uint32 RadixPrefix(std::string_view rest)
		{
			if (rest.size() < 2 || rest[0] != '0') return 10;
			if (rest[1] == 'x' || rest[1] == 'X') return 16;
			if (rest[1] == 'b' || rest[1] == 'B') return 2;
			return 10;
		}

		// `.5` after digits, and `e[+-]digits`: a '.' or 'e' without digits after it is not part of the number.
		size_t DecimalFloatEnd(std::string_view rest, size_t i, bool& isFloat)
		{
			if (i + 1 < rest.size() && rest[i] == '.' && IsDecimalDigit(rest[i + 1]))
			{
				isFloat = true;
				i = SkipWhile(rest, i + 1, IsDecimalDigit);
			}
			if (i < rest.size() && (rest[i] == 'e' || rest[i] == 'E'))
			{
				size_t j = i + 1;
				if (j < rest.size() && (rest[j] == '+' || rest[j] == '-')) ++j;
				if (j < rest.size() && IsDecimalDigit(rest[j]))
				{
					isFloat = true;
					i = SkipWhile(rest, j, IsDecimalDigit);
				}
			}
			return i;
		}

		// `u`, `l`, `ll` and one `u` with `l` / `ll` on either side; `ll` is `ll` or `LL`, never mixed.
		bool IsIntegerSuffix(std::string_view suffix)
		{
			if (!suffix.empty() && (suffix.front() == 'u' || suffix.front() == 'U')) suffix.remove_prefix(1);
			else if (!suffix.empty() && (suffix.back() == 'u' || suffix.back() == 'U')) suffix.remove_suffix(1);
			return suffix.empty() || suffix == "l" || suffix == "L" || suffix == "ll" || suffix == "LL";
		}

		bool IsFloatSuffix(std::string_view suffix) { return suffix.empty() || suffix == "f" || suffix == "F"; }

		char DigitOutsideRadix(const NumberSpelling& spelling)
		{
			const char limit = spelling.radix == 2 ? '1' : spelling.radix == 8 ? '7' : '9';
			if (spelling.radix == 16) return 0;
			for (char c : spelling.digits)
				if (IsDecimalDigit(c) && c > limit) return c;
			return 0;
		}

		LiteralFacts Issue(LiteralIssue issue, uint32 detail = 0)
		{
			LiteralFacts facts;
			facts.issue = issue;
			facts.detail = detail;
			return facts;
		}

		LiteralFacts NumberFacts(const NumberSpelling& spelling)
		{
			if (spelling.digits.empty()) return Issue(LiteralIssue::MissingDigits, spelling.radix == 16 ? 'x' : 'b');
			if (spelling.digits.front() == '.') return Issue(LiteralIssue::NoLeadingDigit);
			if (const char bad = DigitOutsideRadix(spelling)) return Issue(LiteralIssue::InvalidDigit, static_cast<uint8>(bad));
			const uint32 suffixStart = static_cast<uint32>(spelling.length - spelling.suffix.size());
			if (spelling.isFloat)
				return IsFloatSuffix(spelling.suffix) ? LiteralFacts{} : Issue(LiteralIssue::InvalidSuffix, suffixStart);
			if (IsIntegerSuffix(spelling.suffix)) return LiteralFacts{};
			if (spelling.suffix == "f" || spelling.suffix == "F") return Issue(LiteralIssue::FloatSuffixOnInteger);
			if (spelling.radix == 10 && (spelling.suffix.front() == 'e' || spelling.suffix.front() == 'E'))
				return Issue(LiteralIssue::MissingDigits, 'e');
			return Issue(LiteralIssue::InvalidSuffix, suffixStart);
		}

		Core::String Hex(uint32 value, int width)
		{
			static const char* kDigits = "0123456789ABCDEF";
			Core::String text;
			for (int shift = (width - 1) * 4; shift >= 0; shift -= 4)
				text += kDigits[(value >> shift) & 0xF];
			return text;
		}

		const char* UnitName(uint8 unitWidth) { return unitWidth == 1 ? "char8" : unitWidth == 2 ? "char16" : "char32"; }
		const char* RadixName(uint32 radix) { return radix == 2 ? "binary" : radix == 8 ? "octal (it starts with 0)" : "decimal"; }
	}

	NumberSpelling SplitNumber(std::string_view rest)
	{
		NumberSpelling spelling;
		spelling.radix = RadixPrefix(rest);
		const size_t digitsStart = spelling.radix == 10 ? 0 : 2;
		size_t i = SkipWhile(rest, digitsStart, spelling.radix == 16 ? IsHexDigit : IsDecimalDigit);
		if (spelling.radix == 10)
			i = DecimalFloatEnd(rest, i, spelling.isFloat);
		spelling.digits = rest.substr(digitsStart, i - digitsStart);
		if (spelling.radix == 10 && !spelling.isFloat && spelling.digits.size() > 1 && spelling.digits[0] == '0')
			spelling.radix = 8;
		const size_t end = SkipWhile(rest, i, IsSuffixCharacter);
		spelling.suffix = rest.substr(i, end - i);
		spelling.length = end;
		return spelling;
	}

	ScannedLiteral ScanNumber(std::string_view rest)
	{
		const NumberSpelling spelling = SplitNumber(rest);
		ScannedLiteral literal;
		literal.type = TokenType::Number;
		literal.text = Core::String(rest.substr(0, spelling.length));
		literal.length = spelling.length;
		literal.facts = NumberFacts(spelling);
		return literal;
	}

	size_t CodeUnitCount(const Core::String& units, uint8 unitWidth)
	{
		return units.size() / unitWidth;
	}

	uint32 CodeUnitAt(const Core::String& units, uint8 unitWidth, size_t index)
	{
		uint32 value = 0;
		for (uint8 b = 0; b < unitWidth; ++b)
			value |= static_cast<uint32>(static_cast<unsigned char>(units[index * unitWidth + b])) << (8 * b);
		return value;
	}

	Core::String LiteralIssueMessage(const Token& token)
	{
		const LiteralFacts& facts = token.GetLiteralFacts();
		const Core::String& text = token.GetText();
		const char detail = static_cast<char>(facts.detail);
		switch (facts.issue)
		{
		case LiteralIssue::InvalidSuffix:
			return "'" + text + "' has the invalid suffix '" + text.substr(facts.detail) +
				"': an integer takes u, l or ll (and u with l or ll), a floating-point number takes f";
		case LiteralIssue::NoLeadingDigit:
			return "'" + text + "' needs a digit before the '.': write 0" + text;
		case LiteralIssue::FloatSuffixOnInteger:
			return "'" + text + "' is an integer: a float literal needs a '.' or an exponent, write " +
				text.substr(0, text.size() - 1) + ".0f";
		case LiteralIssue::InvalidDigit:
			return "'" + text + "' is " + RadixName(SplitNumber(text).radix) + " and cannot hold the digit '" + Core::String(1, detail) + "'";
		case LiteralIssue::MissingDigits:
			return detail == 'e' ? "'" + text + "' has no digits in its exponent"
				: "'" + text + "' has no digits after its '0" + Core::String(1, detail) + "' prefix";
		case LiteralIssue::UnknownEscape:
			if (detail == 'x') return "the escape '\\x' needs at least one hex digit";
			if (detail == '\n') return "a literal cannot continue on the next line: close it and start a new one (adjacent strings join)";
			return "unknown escape sequence '\\" + Core::String(1, detail) + "'";
		case LiteralIssue::BadUniversalEscape:
			return detail == 'u' ? Core::String("'\\u' takes exactly 4 hex digits naming a code point that is not a surrogate (D800-DFFF)")
				: Core::String("'\\U' takes exactly 8 hex digits naming a code point up to 10FFFF that is not a surrogate (D800-DFFF)");
		case LiteralIssue::EscapeOutOfRange:
			return "the escape value 0x" + Hex(facts.detail, 3) + " does not fit in a " + UnitName(facts.unitWidth) + " code unit";
		case LiteralIssue::Unterminated:
			return detail == '"' ? Core::String("missing the closing '\"' of a string literal on this line")
				: Core::String("missing the closing ''' of a character literal on this line");
		case LiteralIssue::EmptyCharacter:
			return "an empty character literal: write '\\0' for the NUL character";
		case LiteralIssue::MultiCharacter:
			return "a character literal holds one character: use a string literal for more";
		case LiteralIssue::CharacterOutOfRange:
			if (facts.detail > 0xFFFF)
				return "U+" + Hex(facts.detail, 6) + " does not fit in one " + UnitName(facts.unitWidth) +
					": write U'\\U" + Hex(facts.detail, 8) + "' (char32)";
			return "U+" + Hex(facts.detail, 4) + " does not fit in one " + UnitName(facts.unitWidth) +
				": write u'\\u" + Hex(facts.detail, 4) + "' (char16) or U'\\U" + Hex(facts.detail, 8) + "' (char32)";
		case LiteralIssue::InvalidUtf8:
			return "the source bytes of this wide literal are not UTF-8";
		case LiteralIssue::None:
			break;
		}
		return "";
	}
}
