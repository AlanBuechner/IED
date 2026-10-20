#include "IfStatementNode.h"
#include "AsmStatementNode.h"
#include "CodeBlockNode.h"
#include "ExpressionStatementNode.h"
#include "EmptyStatementNode.h"
#include "ForLoopNode.h"
#include "WhileLoopNode.h"
#include "SimpleStatementNodes.h"
#include "SwitchStatementNode.h"
#include "TryCatchNode.h"
#include "../Declarations/VariableDefinitionNode.h"
#include "../Declarations/FunctionDefinitionNode.h"
#include "../Declarations/ObjectDefinitionNode.h"
#include "../Declarations/EnumDefinitionNode.h"
#include "../Declarations/UnionDefinitionNode.h"
#include "../Declarations/AliasDefinitionNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> IfStatementNode::Children() const
	{
		return m_Children;
	}

	Ref<AstNode> ParseAnyStatement(ParserState& state)
	{
#define TRY_PARSE_STMT(type) \
		{ auto node = type::TryParse(state); \
		if (node != nullptr) return node; }

		// Declarations first (can appear in blocks)
		TRY_PARSE_STMT(VariableDefinitionNode);
		TRY_PARSE_STMT(FunctionDefinitionNode);
		TRY_PARSE_STMT(ObjectDefinitionNode);
		TRY_PARSE_STMT(EnumDefinitionNode);
		TRY_PARSE_STMT(UnionDefinitionNode);
		TRY_PARSE_STMT(AliasDefinitionNode);

		// Keyword-based control flow (keyword first)
		TRY_PARSE_STMT(IfStatementNode);
		TRY_PARSE_STMT(WhileLoopNode);
		TRY_PARSE_STMT(ForLoopNode);
		TRY_PARSE_STMT(DoWhileLoopNode);
		TRY_PARSE_STMT(SwitchStatementNode);
		TRY_PARSE_STMT(TryCatchNode);
		TRY_PARSE_STMT(ReturnStatementNode);
		TRY_PARSE_STMT(BreakStatementNode);
		TRY_PARSE_STMT(ContinueStatementNode);
		TRY_PARSE_STMT(DeferStatementNode);
		TRY_PARSE_STMT(AsmStatementNode);
		TRY_PARSE_STMT(FallthroughStatementNode);
		TRY_PARSE_STMT(GotoStatementNode);
		TRY_PARSE_STMT(ThrowStatementNode);

		// Structural statements
		TRY_PARSE_STMT(CodeBlockNode);          // standalone scope {...}
		TRY_PARSE_STMT(LabelStatementNode);     // identifier: statement
		TRY_PARSE_STMT(EmptyStatementNode);     // bare semicolon
		TRY_PARSE_STMT(ExpressionStatementNode);// catch-all expression + ;

#undef TRY_PARSE_STMT

		return nullptr;
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
