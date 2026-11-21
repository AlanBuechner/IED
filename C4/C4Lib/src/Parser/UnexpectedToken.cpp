#include "Parser/UnexpectedToken.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	namespace
	{
		// A token that ends a declared name: `x = 1`, `x;`, `x, y`, `x(`, `x[`, `x)`.
		bool EndsAName(TokenType t)
		{
			return t == TokenType::Equal || t == TokenType::SemiColon || t == TokenType::Comma || t == TokenType::OpenParen ||
				t == TokenType::OpenBracket || t == TokenType::CloseParen;
		}

		// C's array declarator `int values[4]` (BUG-361): in C4 the extent belongs to the type.
		Core::String CArrayDeclaratorMessage(const Lexer& lexer)
		{
			const Token& type = lexer.PeekCompound();
			const Token& name = lexer.PeekCompound(1);
			if (type.GetType() != TokenType::Identifier || name.GetType() != TokenType::Identifier ||
				lexer.PeekCompound(2).GetType() != TokenType::OpenBracket)
				return "";
			const bool oneToken = lexer.PeekCompound(4).GetType() == TokenType::CloseBracket;
			const Core::String extent = oneToken ? Core::String(lexer.PeekCompound(3).GetText()) : Core::String("N");
			return Core::StringFormat::Format("an array's extent belongs to its type in C4: write '{0}[{1}] {2}', not '{0} {2}[{1}]'",
				type.GetText(), extent, name.GetText());
		}

		// C++'s brace initialization `Point p{ 1, 2 }`: in C4 a declaration is initialized with '='.
		Core::String BraceDeclaratorMessage(const Lexer& lexer)
		{
			const Token& type = lexer.PeekCompound();
			const Token& name = lexer.PeekCompound(1);
			if (type.GetType() != TokenType::Identifier || name.GetType() != TokenType::Identifier ||
				lexer.PeekCompound(2).GetType() != TokenType::OpenCurl)
				return "";
			return "a declaration is initialized with '=' in C4: write '" + Core::String(type.GetText()) + " " + Core::String(name.GetText()) +
				" = { ... };', not '" + Core::String(type.GetText()) + " " + Core::String(name.GetText()) + "{ ... }'";
		}

		constexpr uint32 kDeclarationScanTokens = 64;

		// A token that cannot start a parameter type of a signature, but starts a call's argument: `3`, `"a"`, `-1`, `(`.
		bool StartsOnlyAnArgument(TokenType t)
		{
			return t == TokenType::Number || t == TokenType::Text || t == TokenType::Char || t == TokenType::Minus ||
				t == TokenType::OpenParen || t == TokenType::True || t == TokenType::False;
		}

		// The text of the tokens [from, to], spaced as written closely enough for a message: `Ns::Square(3, 4)`.
		Core::String TokensText(const Lexer& lexer, uint32 from, uint32 to)
		{
			Core::String text;
			for (uint32 i = from; i <= to; ++i)
			{
				if (i > from && lexer.PeekCompound(i - 1).GetType() == TokenType::Comma) text += " ";
				text += lexer.PeekCompound(i).GetText();
			}
			return text;
		}

		// Where the call that starts at `name` (a name, maybe qualified, then `(`) ends: its `)`, or 0 when it does not close.
		uint32 CallEnd(const Lexer& lexer, uint32 open)
		{
			int depth = 0;
			for (uint32 i = open; i < open + kDeclarationScanTokens; ++i)
			{
				const TokenType t = lexer.PeekCompound(i).GetType();
				if (t == TokenType::OpenParen) ++depth;
				else if (t == TokenType::CloseParen && --depth == 0) return i;
				else if (t == TokenType::EndOfFile || t == TokenType::SemiColon || t == TokenType::OpenCurl || t == TokenType::CloseCurl) return 0;
			}
			return 0;
		}
	}

	Core::String UnparenthesizedCallMessage(const Lexer& lexer)
	{
		for (uint32 at = 0; at < kDeclarationScanTokens; ++at)
		{
			const TokenType t = lexer.PeekCompound(at).GetType();
			if (t == TokenType::EndOfFile || t == TokenType::SemiColon || t == TokenType::OpenCurl || t == TokenType::CloseCurl) return "";
			// `<` after a template's name, or `,` between its arguments; a comparison's `a < f(3)` is not followed by `>`.
			if (t != TokenType::Less && t != TokenType::Comma) continue;
			if (t == TokenType::Less && (at == 0 || lexer.PeekCompound(at - 1).GetType() != TokenType::Identifier)) continue;
			uint32 name = at + 1;
			uint32 open = name;
			while (lexer.PeekCompound(open).GetType() == TokenType::Identifier && lexer.PeekCompound(open + 1).GetType() == TokenType::DoubleColon) open += 2;
			if (lexer.PeekCompound(open).GetType() != TokenType::Identifier) continue;
			++open;
			if (lexer.PeekCompound(open).GetType() != TokenType::OpenParen || !StartsOnlyAnArgument(lexer.PeekCompound(open + 1).GetType())) continue;
			const uint32 close = CallEnd(lexer, open);
			const TokenType after = close == 0 ? TokenType::None : lexer.PeekCompound(close + 1).GetType();
			if (after != TokenType::Greater && after != TokenType::Comma) continue;
			const Core::String call = TokensText(lexer, name, close);
			return "a call in a template argument is written in parentheses: '(" + call + ")', not '" + call +
				"' - there it reads as a function signature, as in 'funcptr<int32(int32)>' (docs/language/08)";
		}
		return "";
	}

	Core::String UnexpectedTokenMessage(const Lexer& lexer)
	{
		const Token& token = lexer.PeekCompound();
		const Token& name = lexer.PeekCompound(1);
		if (token.GetType() == TokenType::Identifier && Token::IsKeyword(name.GetType()) && EndsAName(lexer.PeekCompound(2).GetType()))
			return KeywordNameMessage(name);
		if (Core::String array = CArrayDeclaratorMessage(lexer); !array.empty()) return array;
		if (Core::String braces = BraceDeclaratorMessage(lexer); !braces.empty()) return braces;
		if (Core::String call = UnparenthesizedCallMessage(lexer); !call.empty()) return call;
		return Core::StringFormat::Format("Unexpected token '{0}'", token.GetText());
	}

	bool KeywordNamesAVariable(const Lexer& lexer)
	{
		const TokenType after = lexer.PeekCompound(1).GetType();
		return Token::IsKeyword(lexer.PeekCompound().GetType()) &&
			(after == TokenType::Equal || after == TokenType::SemiColon || after == TokenType::Comma);
	}

	// Only braces count: they are the block structure, while broken code leaves parentheses and brackets unmatched. The
	// token nothing took is always skipped, so a caller that meets it again makes progress (a stray `}` at file scope).
	void SkipRestOfStatement(Lexer& lexer)
	{
		int braces = 0;
		for (bool first = true;; first = false)
		{
			const TokenType type = lexer.PeekCompound().GetType();
			if (type == TokenType::EndOfFile || (type == TokenType::CloseCurl && braces == 0 && !first)) return;
			lexer.NextCompoundToken();
			if (first && type == TokenType::CloseCurl) return;
			if (type == TokenType::SemiColon && braces == 0) return;
			if (type == TokenType::OpenCurl) ++braces;
			else if (type == TokenType::CloseCurl) --braces;
		}
	}

	Core::String KeywordNameMessage(const Token& name)
	{
		return Core::StringFormat::Format("'{0}' is a keyword: it cannot name a variable, parameter or function", name.GetText());
	}
}
