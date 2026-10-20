#include "LexerRecovery.h"
#include "Core/Core.h"

namespace Scripting
{

	const Token& LexerRecovery::RecoverToTokenType(Lexer& lexer, TokenType targetType)
	{
		return RecoverUntil(lexer, [targetType](const Token& token) -> bool
		{
			return token.GetType() == targetType;
		});
	}

	const Token& LexerRecovery::RecoverToAnyTokenType(Lexer& lexer, const Core::Vector<TokenType>& targetTypes)
	{
		return RecoverUntil(lexer, [&targetTypes](const Token& token) -> bool
		{
			for (const auto& type : targetTypes)
			{
				if (token.GetType() == type)
					return true;
			}
			return false;
		});
	}

	const Token& LexerRecovery::RecoverToEndOfScope(Lexer& lexer, TokenType openType, TokenType closeType)
	{
		int32 depth = 0; // Track nesting depth; increments on open, decrements on close. Stops at 0.

		while (true)
		{
			const Token& token = lexer.NextCompoundToken();

			if (token.GetType() == TokenType::EndOfFile)
				return token;

			if (token.GetType() == openType)
				depth++;
			else if (token.GetType() == closeType)
			{
				depth--;
				if (depth == 0)
					return token;
			}
		}
	}

	const Token& LexerRecovery::RecoverToStatementBoundary(Lexer& lexer)
	{
		return RecoverUntil(lexer, [](const Token& token) -> bool
		{
			return token.GetType() == TokenType::SemiColon ||
			       token.GetType() == TokenType::CloseCurl ||
			       token.GetType() == TokenType::EndOfFile;
		});
	}

} // namespace Scripting
