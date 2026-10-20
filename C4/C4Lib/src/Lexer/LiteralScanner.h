#pragma once
#include "Lexer/Token.h"
#include <string_view>

namespace Scripting
{
	/// A numeric literal's spelling split into its parts: `0x1Fu` is radix 16, digits `1F`, suffix `u`. The
	/// lexer and literal typing split the same way, so a suffix is never read as a digit or the reverse.
	struct NumberSpelling
	{
		uint32 radix = 10;       // 2, 8 (a leading 0), 10 or 16
		bool isFloat = false;    // a fraction or an exponent (decimal only)
		std::string_view digits; // the integer digits, or a float's mantissa and exponent
		std::string_view suffix; // the identifier characters after the number
		size_t length = 0;       // the whole spelling
	};

	/// A literal read from the source: its token type and text (a number's spelling; a character or
	/// string's code units, `unitWidth` little-endian bytes each), how many source bytes it spans and the
	/// facts the parse that consumes it reports.
	struct ScannedLiteral
	{
		TokenType type = TokenType::None;
		Core::String text;
		size_t length = 0;
		LiteralFacts facts;
	};

	/// `rest` starts with a digit, or with a '.' before a digit (`.5`, scanned whole so its error names it).
	NumberSpelling SplitNumber(std::string_view rest);
	ScannedLiteral ScanNumber(std::string_view rest);

	/// The prefix length (0 for `'` or `"`, 1 for `u` / `U`, 2 for `u8`) when `rest` starts a character or
	/// string literal, else npos.
	size_t QuotedLiteralPrefix(std::string_view rest);
	ScannedLiteral ScanQuoted(std::string_view rest, size_t prefixLength);

	/// The code units of a character or string token's text.
	size_t CodeUnitCount(const Core::String& units, uint8 unitWidth);
	uint32 CodeUnitAt(const Core::String& units, uint8 unitWidth, size_t index);

	/// The diagnostic for a literal token whose facts carry an issue.
	Core::String LiteralIssueMessage(const Token& token);
}
