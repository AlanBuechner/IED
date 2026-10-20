#pragma once

#include "Lexer.h"

namespace Scripting
{
	class LexerRecovery
	{
	public:
		template<typename Predicate>
		static const Token& RecoverUntil(Lexer& lexer, Predicate predicate);

		static const Token& RecoverToTokenType(Lexer& lexer, TokenType targetType);
		static const Token& RecoverToAnyTokenType(Lexer& lexer, const Core::Vector<TokenType>& targetTypes);
		static const Token& RecoverToEndOfScope(Lexer& lexer, TokenType openType, TokenType closeType);
		static const Token& RecoverToStatementBoundary(Lexer& lexer);
	};

	template<typename Predicate>
	const Token& LexerRecovery::RecoverUntil(Lexer& lexer, Predicate predicate)
	{
		while (true)
		{
			const Token& token = lexer.NextCompoundToken();
			if (token.GetType() == TokenType::EndOfFile || predicate(token))
				return token;
		}
	}

} // namespace Scripting
