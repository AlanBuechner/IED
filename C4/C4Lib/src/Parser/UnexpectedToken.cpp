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
	}

	Core::String UnexpectedTokenMessage(const Lexer& lexer)
	{
		const Token& token = lexer.PeekCompound();
		const Token& name = lexer.PeekCompound(1);
		if (token.GetType() == TokenType::Identifier && Token::IsKeyword(name.GetType()) && EndsAName(lexer.PeekCompound(2).GetType()))
			return KeywordNameMessage(name);
		if (Core::String array = CArrayDeclaratorMessage(lexer); !array.empty()) return array;
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
