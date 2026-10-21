#include "SwitchStatementNode.h"
#include "StatementParser.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{

Core::String SwitchCaseNode::ToString() const
{
	if (m_Kind == CaseKind::Default)
		return "default";
	return Core::StringFormat::Format("case {0}", m_ConstantExpr ? m_ConstantExpr->ToString() : "?");
}

std::span<const Ref<AstNode>> SwitchStatementNode::Children() const
{
	return m_Children;
}

Ref<SwitchStatementNode> SwitchStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Switch)
		return nullptr;

	auto node = CreateRef<SwitchStatementNode>();
	node->ParseBody(state);
	return node;
}

void SwitchStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_Condition = nullptr;
	m_Cases.Clear();

	// 1. Consume 'switch'
	Token switchToken = lexer.PeekCompound();
	m_SwitchToken = switchToken;
	startLocation = switchToken.GetStartLocation();
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
		errorTracker.AddError(openParen.GetStartLocation(), "Expected '(' after 'switch'", ErrorSeverity::Error);
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
		errorTracker.AddError(closeParen.GetStartLocation(), "Expected ')' after switch condition", ErrorSeverity::Error);
	}

	// 5. Expect '{'
	Token openBrace = lexer.PeekCompound();
	if (openBrace.GetType() == TokenType::OpenCurl)
	{
		m_OpenBraceToken = openBrace;
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(openBrace.GetStartLocation(), "Expected '{' for switch body", ErrorSeverity::Error);
		return;
	}

	// 6. Parse cases until '}' (comments are transparent - PeekCompound skips them)
	while (true)
	{
		Token t = lexer.PeekCompound();

		if (t.GetType() == TokenType::CloseCurl || t.GetType() == TokenType::EndOfFile)
			break;

		auto caseNode = CreateRef<SwitchCaseNode>();
		caseNode->startLocation = t.GetStartLocation();

		if (t.GetType() == TokenType::Case)
		{
			caseNode->m_Kind = SwitchCaseNode::CaseKind::Case;
			caseNode->m_CaseKeywordToken = t;
			lexer.NextCompoundToken();

			// Parse case expression
			caseNode->m_ConstantExpr = PrattParser::ParseExpression(state);

			// Expect ':'
			Token colon = lexer.PeekCompound();
			if (colon.GetType() == TokenType::Colon)
			{
				caseNode->m_ColonToken = colon;
				lexer.NextCompoundToken();
			}
			else
			{
				errorTracker.AddError(
					colon.GetStartLocation(),
					Core::StringFormat::Format("Expected ':' after case expression, got '{0}'", colon.GetText()),
					ErrorSeverity::Error);
			}

			caseNode->endLocation = caseNode->m_ColonToken.GetEndLocation();
			m_Cases.Push(caseNode);
		}
		else if (t.GetType() == TokenType::Default)
		{
			caseNode->m_Kind = SwitchCaseNode::CaseKind::Default;
			caseNode->m_CaseKeywordToken = t;
			lexer.NextCompoundToken();

			Token colon = lexer.PeekCompound();
			if (colon.GetType() == TokenType::Colon)
			{
				caseNode->m_ColonToken = colon;
				lexer.NextCompoundToken();
			}
			else
			{
				errorTracker.AddError(
					colon.GetStartLocation(),
					Core::StringFormat::Format("Expected ':' after 'default', got '{0}'", colon.GetText()),
					ErrorSeverity::Error);
			}

			caseNode->endLocation = caseNode->m_ColonToken.GetEndLocation();
			m_Cases.Push(caseNode);
		}
		else
		{
			// Statements within current case - add to last case
			if (!m_Cases.Empty())
			{
				auto stmt = ParseAnyStatement(state);
				if (stmt)
					m_Cases[m_Cases.Count() - 1]->m_Statements.Push(stmt);
				else
				{
					errorTracker.AddError(
						t.GetStartLocation(),
						Core::StringFormat::Format("Unexpected token '{0}' in switch body", t.GetText()),
						ErrorSeverity::Error);
					lexer.NextCompoundToken();
				}
			}
			else
			{
				errorTracker.AddError(
					t.GetStartLocation(),
					Core::StringFormat::Format("Unexpected token '{0}' in switch body", t.GetText()),
					ErrorSeverity::Error);
				lexer.NextCompoundToken();
			}
		}
	}

	// 7. Close brace
	Token closeBrace = lexer.PeekCompound();
	if (closeBrace.GetType() == TokenType::CloseCurl)
	{
		m_CloseBraceToken = closeBrace;
		endLocation = closeBrace.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else if (closeBrace.GetType() != TokenType::EndOfFile)
	{
		errorTracker.AddError(
			closeBrace.GetStartLocation(),
			Core::StringFormat::Format("Expected '}}' to close switch, got '{0}'", closeBrace.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}

	if (endLocation.line == 0)
		endLocation = startLocation;

	m_Children.Clear();
	if (m_Condition)
		m_Children.Push(m_Condition);
	for (uint32 i = 0; i < m_Cases.Count(); ++i)
		m_Children.Push(m_Cases[i]);
}

} // namespace Scripting
