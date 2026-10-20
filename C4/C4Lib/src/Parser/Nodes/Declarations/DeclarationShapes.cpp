#include "DeclarationShapes.h"
#include "ParameterNode.h"
#include "QualTypeNode.h"

namespace Scripting
{
	bool ParenGroupIsFollowedByBody(Lexer& lexer)
	{
		auto undo = LexerUndoScope(lexer);
		int depth = 0;
		for (;;)
		{
			Token t = lexer.NextCompoundToken();
			if (t.GetType() == TokenType::EndOfFile) return false;
			if (t.GetType() == TokenType::OpenParen) ++depth;
			else if (t.GetType() == TokenType::CloseParen && --depth == 0) break;
		}
		return lexer.PeekCompound().GetType() == TokenType::OpenCurl;
	}

	bool ParenGroupIsParameterList(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		auto undo = LexerUndoScope(lexer);
		ErrorTracker probeErrors;
		ParserState probe(lexer, probeErrors);
		lexer.NextCompoundToken(); // '('
		for (;;)
		{
			const TokenType next = lexer.PeekCompound().GetType();
			if (next == TokenType::CloseParen || next == TokenType::Ellipsis) return true;
			if (!ParameterNode::TryParse(probe) || probeErrors.HasErrors()) return false;
			const TokenType after = lexer.PeekCompound().GetType();
			if (after == TokenType::CloseParen) return true;
			if (after != TokenType::Comma) return false;
			lexer.NextCompoundToken();
		}
	}
} // namespace Scripting
