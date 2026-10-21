#include "WhileLoopNode.h"
#include "CodeBlockNode.h"
#include "StatementParser.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> WhileLoopNode::Children() const
	{
		return m_Children;
	}

	Ref<WhileLoopNode> WhileLoopNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token t = lexer.PeekCompound();
		if (t.GetType() != TokenType::While)
			return nullptr;

		auto node = CreateRef<WhileLoopNode>();
		node->ParseBody(state);
		return node;
	}

	void WhileLoopNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_Body = nullptr;
		m_SingleStatementBody = nullptr;

		// 1. Consume 'while'
		Token whileToken = lexer.PeekCompound();
		m_WhileToken = whileToken;
		startLocation = whileToken.GetStartLocation();
		lexer.NextCompoundToken();

		// 2. Expect '('
		Token openParen = lexer.PeekCompound();
		if (openParen.GetType() == TokenType::OpenParen)
		{
			m_OpenParenToken = openParen;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(openParen.GetStartLocation(), "Expected '(' after 'while'", ErrorSeverity::Error);
		}

		// 3. Parse condition
		m_Condition = PrattParser::ParseExpression(state);

		// 4. Expect ')'
		Token closeParen = lexer.PeekCompound();
		if (closeParen.GetType() == TokenType::CloseParen)
		{
			m_CloseParenToken = closeParen;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(closeParen.GetStartLocation(), "Expected ')' after while condition", ErrorSeverity::Error);
		}

		// 5. Parse body
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenCurl)
		{
			m_Body = CodeBlockNode::TryParse(state);
		}
		else
		{
			m_SingleStatementBody = ParseAnyStatement(state);
		}

		if (m_Body)
			endLocation = m_Body->endLocation;
		else if (m_SingleStatementBody)
			endLocation = m_SingleStatementBody->endLocation;
		else
			endLocation = startLocation;

		m_Children.Clear();
		if (m_Condition)
			m_Children.Push(m_Condition);
		if (m_Body)
			m_Children.Push(m_Body);
		if (m_SingleStatementBody)
			m_Children.Push(m_SingleStatementBody);
	}

} // namespace Scripting
