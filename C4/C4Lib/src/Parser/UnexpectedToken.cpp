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
	}

	Core::String UnexpectedTokenMessage(const Lexer& lexer)
	{
		const Token& token = lexer.PeekCompound();
		const Token& name = lexer.PeekCompound(1);
		if (token.GetType() == TokenType::Identifier && Token::IsKeyword(name.GetType()) && EndsAName(lexer.PeekCompound(2).GetType()))
			return Core::StringFormat::Format("'{0}' is a keyword: it cannot name a variable, parameter or function", name.GetText());
		return Core::StringFormat::Format("Unexpected token '{0}'", token.GetText());
	}
}
