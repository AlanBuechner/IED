#include "Parser/Recovery.h"

namespace Scripting
{
	namespace
	{
		bool StartsDeclaration(const Token& token)
		{
			switch (token.GetType())
			{
			case TokenType::Namespace:
			case TokenType::Class:
			case TokenType::Struct:
			case TokenType::Interface:
			case TokenType::Enum:
			case TokenType::Union:
			case TokenType::Alias:
			case TokenType::Template:
			case TokenType::Extern:
				return true;
			default:
				return token.GetType() == TokenType::Identifier && token.GetText() == "include";
			}
		}

		bool StartsMember(const Token& token)
		{
			switch (token.GetType())
			{
			case TokenType::Public:
			case TokenType::Private:
			case TokenType::Protected:
			case TokenType::Static:
			case TokenType::Virtual:
			case TokenType::OpenBracket: // an annotation
				return true;
			default:
				return StartsDeclaration(token) && token.GetType() != TokenType::Namespace && token.GetType() != TokenType::Extern;
			}
		}

		bool StartsStatement(const Token& token)
		{
			switch (token.GetType())
			{
			case TokenType::If:
			case TokenType::For:
			case TokenType::While:
			case TokenType::Do:
			case TokenType::Switch:
			case TokenType::Return:
			case TokenType::Break:
			case TokenType::Continue:
			case TokenType::Throw:
			case TokenType::Try:
			case TokenType::Defer:
			case TokenType::Goto:
			case TokenType::Class:
			case TokenType::Struct:
			case TokenType::Enum:
			case TokenType::Union:
			case TokenType::Alias:
				return true;
			default:
				return false;
			}
		}

		bool StartsNext(Resume resume, const Token& token)
		{
			switch (resume)
			{
			case Resume::Declaration: return StartsDeclaration(token);
			case Resume::Member: return StartsMember(token);
			case Resume::Statement: return StartsStatement(token);
			}
			return false;
		}
	}

	Ref<ErrorNode> SkipBroken(Lexer& lexer, Resume resume, Core::String why)
	{
		const Token first = lexer.PeekCompound();
		SourceLocation end = first.GetStartLocation();
		uint32 lastLine = first.GetStartLocation().line;
		int depth = 0;  // braces: the block structure, which broken code keeps
		int parens = 0; // parentheses and brackets, which broken code leaves unmatched: they only keep a `{ }` group (a
		                // lambda's body in a call) from ending the skip
		for (bool atFirst = true;; atFirst = false)
		{
			const Token token = lexer.PeekCompound();
			const TokenType type = token.GetType();
			if (type == TokenType::EndOfFile) break;
			if (type == TokenType::CloseCurl && depth == 0 && !atFirst) break;
			if (!atFirst && depth == 0 && token.GetStartLocation().line > lastLine && StartsNext(resume, token)) break;
			lexer.NextCompoundToken();
			end = token.GetEndLocation();
			lastLine = end.line;
			if (type == TokenType::CloseCurl && depth == 0) break; // a stray `}`: the caller met it at the root
			if (type == TokenType::SemiColon && depth == 0) break;
			if (type == TokenType::OpenParen || type == TokenType::OpenBracket) ++parens;
			else if ((type == TokenType::CloseParen || type == TokenType::CloseBracket) && parens > 0) --parens;
			else if (type == TokenType::OpenCurl) ++depth;
			else if (type == TokenType::CloseCurl && --depth == 0 && parens == 0)
			{
				const Token after = lexer.PeekCompound();
				if (resume != Resume::Statement && after.GetType() == TokenType::SemiColon)
				{
					end = lexer.NextCompoundToken().GetEndLocation();
					break;
				}
				if (after.GetStartLocation().line > lastLine) break; // the group ended its line: so did the broken piece
			}
		}
		return CreateRef<ErrorNode>(first.GetStartLocation(), end, std::move(why));
	}
}
