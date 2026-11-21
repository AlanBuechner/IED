#include "Macro/MacroToken.h"
#include "Macro/MacroText.h"

namespace Scripting
{
	using namespace MacroText;

	namespace
	{
		bool IsDigit(char c) { return c >= '0' && c <= '9'; }
	}

	bool StartsMacroNumber(std::string_view text, size_t at)
	{
		return at < text.size() && (IsDigit(text[at]) || (text[at] == '.' && at + 1 < text.size() && IsDigit(text[at + 1])));
	}

	// Digits, letters and `_`, a `.` not before another (`1.5`, but `2...N` is a range), and the sign of an exponent
	// (`1e+5`, `0x1p-3`; `0xE+1` is two numbers).
	size_t MacroNumberEnd(std::string_view text, size_t at)
	{
		const bool hex = text.size() > at + 1 && text[at] == '0' && (text[at + 1] == 'x' || text[at + 1] == 'X');
		size_t end = at;
		while (end < text.size())
		{
			const char c = text[end];
			if (IsIdentChar(c)) { ++end; continue; }
			if (c == '.' && !(end + 1 < text.size() && text[end + 1] == '.')) { ++end; continue; }
			const char before = end > at ? text[end - 1] : '\0';
			const bool exponent = hex ? (before == 'p' || before == 'P') : (before == 'e' || before == 'E');
			if ((c == '+' || c == '-') && exponent) { ++end; continue; }
			break;
		}
		return end;
	}

	namespace
	{
		MacroToken::Kind KindAt(std::string_view text, size_t at)
		{
			if (StartsLiteral(text, at)) return MacroToken::Kind::Literal;
			if (StartsMacroNumber(text, at)) return MacroToken::Kind::Number;
			if (IsIdentStart(text[at])) return MacroToken::Kind::Identifier;
			return MacroToken::Kind::Punctuation;
		}

		size_t TokenEnd(std::string_view text, size_t at, MacroToken::Kind kind)
		{
			switch (kind)
			{
			case MacroToken::Kind::Literal: return LiteralEnd(text, at);
			case MacroToken::Kind::Number: return MacroNumberEnd(text, at);
			case MacroToken::Kind::Identifier: return IdentifierEnd(text, at);
			case MacroToken::Kind::Punctuation: return text.compare(at, 2, "##") == 0 ? at + 2 : at + 1;
			}
			return at + 1;
		}
	}

	MacroTokens ReadMacroTokens(std::string_view text, uint32 fileOffset)
	{
		MacroTokens tokens;
		bool space = false;
		for (size_t at = 0; at < text.size();)
		{
			if (IsSpace(text[at])) { space = true; ++at; continue; }
			if (StartsComment(text, at)) { space = true; at = CommentEnd(text, at); continue; }
			MacroToken token;
			token.kind = KindAt(text, at);
			const size_t end = TokenEnd(text, at, token.kind);
			token.spelling.assign(text.substr(at, end - at));
			token.fileOffset = fileOffset == MacroToken::kNotInFile ? MacroToken::kNotInFile : fileOffset + static_cast<uint32>(at);
			token.spaceBefore = space;
			tokens.push_back(std::move(token));
			space = false;
			at = end;
		}
		return tokens;
	}

	std::string MacroTokenText(const MacroTokens& tokens)
	{
		std::string text;
		for (const MacroToken& token : tokens)
		{
			if (token.spaceBefore && !text.empty()) text += ' ';
			text += token.spelling;
		}
		return text;
	}

	MacroToken::Kind KindOfSpelling(std::string_view spelling)
	{
		return spelling.empty() ? MacroToken::Kind::Punctuation : KindAt(spelling, 0);
	}
}
