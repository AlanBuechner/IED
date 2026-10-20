#include "Parser/Nodes/Expressions/ValueListNode.h"
#include "Parser/PrattParser.h"
#include "Compiler/ErrorTracker.h"

namespace Scripting
{
	Ref<ValueListNode> ValueListNode::Parse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		auto node = CreateRef<ValueListNode>();
		node->startLocation = lexer.PeekCompound().GetStartLocation();
		lexer.NextCompoundToken(); // [
		while (lexer.PeekCompound().GetType() != TokenType::CloseBracket && lexer.PeekCompound().GetType() != TokenType::EndOfFile)
		{
			node->m_Elements.Push(PrattParser::ParseArgument(state));
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken();
		}
		const Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseBracket)
		{
			state.GetErrorTracker().AddError(close.GetStartLocation(),
				Core::StringFormat::Format("Expected ']' to close the value list, got '{0}'", close.GetText()), ErrorSeverity::Error);
			return node;
		}
		node->endLocation = close.GetEndLocation();
		lexer.NextCompoundToken();
		return node;
	}

	Core::String ValueListNode::ToString() const
	{
		Core::String result = "[";
		for (uint32 i = 0; i < m_Elements.Count(); ++i) result += (i ? ", " : "") + m_Elements[i]->ToString();
		return result + "]";
	}
} // namespace Scripting
