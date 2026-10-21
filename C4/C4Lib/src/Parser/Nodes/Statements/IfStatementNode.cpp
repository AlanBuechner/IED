#include "IfStatementNode.h"
#include "CodeBlockNode.h"
#include "StatementParser.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> IfStatementNode::Children() const
	{
		return m_Children;
	}

	Ref<IfStatementNode> IfStatementNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token t = lexer.PeekCompound();
		if (t.GetType() != TokenType::If)
			return nullptr;

		auto node = CreateRef<IfStatementNode>();
		node->ParseBody(state);
		return node;
	}

	void IfStatementNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_ThenBody = nullptr;
		m_ThenSingleStatement = nullptr;
		m_ElseBranch = nullptr;

		// 1. Consume 'if'
		Token ifToken = lexer.PeekCompound();
		m_IfToken = ifToken;
		startLocation = ifToken.GetStartLocation();
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
			errorTracker.AddError(
				openParen.GetStartLocation(),
				Core::StringFormat::Format("Expected '(' after 'if', got '{0}'", openParen.GetText()),
				ErrorSeverity::Error);
		}

		// 3. Parse condition expression
		m_Condition = PrattParser::ParseExpression(state);
		if (!m_Condition)
		{
			errorTracker.AddError(lexer.PeekCompound().GetStartLocation(), "Expected condition expression", ErrorSeverity::Error);
		}

		// 4. Expect ')'
		Token closeParen = lexer.PeekCompound();
		if (closeParen.GetType() == TokenType::CloseParen)
		{
			m_CloseParenToken = closeParen;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				closeParen.GetStartLocation(),
				Core::StringFormat::Format("Expected ')' after condition, got '{0}'", closeParen.GetText()),
				ErrorSeverity::Error);
		}

		// 5. Parse then body (code block or single statement)
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenCurl)
		{
			m_ThenBody = CodeBlockNode::TryParse(state);
		}
		else
		{
			m_ThenSingleStatement = ParseAnyStatement(state);
			if (!m_ThenSingleStatement)
			{
				errorTracker.AddError(
					next.GetStartLocation(),
					Core::StringFormat::Format("Expected statement after 'if' condition, got '{0}'", next.GetText()),
					ErrorSeverity::Error);
			}
		}

		// 6. Optional else branch
		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Else)
		{
			lexer.NextCompoundToken(); // consume 'else'
			next = lexer.PeekCompound();

			if (next.GetType() == TokenType::If)
			{
				// else-if chain
				m_ElseBranch = IfStatementNode::TryParse(state);
			}
			else if (next.GetType() == TokenType::OpenCurl)
			{
				auto elseBlock = CodeBlockNode::TryParse(state);
				if (elseBlock)
					m_ElseBranch = elseBlock;
			}
			else
			{
				m_ElseBranch = ParseAnyStatement(state);
				if (!m_ElseBranch)
				{
					errorTracker.AddError(
						next.GetStartLocation(),
						Core::StringFormat::Format("Expected statement after 'else', got '{0}'", next.GetText()),
						ErrorSeverity::Error);
				}
			}
		}

		// Set end location
		if (m_ElseBranch)
			endLocation = m_ElseBranch->endLocation;
		else if (m_ThenBody)
			endLocation = m_ThenBody->endLocation;
		else if (m_ThenSingleStatement)
			endLocation = m_ThenSingleStatement->endLocation;
		else
			endLocation = startLocation;

		// Build children
		m_Children.Clear();
		if (m_Condition)
			m_Children.Push(m_Condition);
		if (m_ThenBody)
			m_Children.Push(m_ThenBody);
		if (m_ThenSingleStatement)
			m_Children.Push(m_ThenSingleStatement);
		if (m_ElseBranch)
			m_Children.Push(m_ElseBranch);
	}

} // namespace Scripting
