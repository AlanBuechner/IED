#include "TryCatchNode.h"
#include "CodeBlockNode.h"
#include "IfStatementNode.h" // for ParseAnyStatement forward
#include "../Declarations/QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{

Core::String CatchClauseNode::ToString() const
{
	Core::String result = "catch ";
	if (m_ExceptionType)
		result += m_ExceptionType->ToString();
	if (HasExceptionName())
	{
		result += " ";
		result += m_ExceptionNameToken.GetText();
	}
	return result;
}

std::span<const Ref<AstNode>> CatchClauseNode::Children() const
{
	return m_Children;
}

std::span<const Ref<AstNode>> TryCatchNode::Children() const
{
	return m_Children;
}

Ref<TryCatchNode> TryCatchNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Try)
		return nullptr;

	auto node = CreateRef<TryCatchNode>();
	node->ParseBody(state);
	return node;
}

void TryCatchNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_TryBody = nullptr;
	m_CatchClauses.Clear();
	m_FinallyBody = nullptr;

	// 1. Consume 'try'
	Token tryToken = lexer.PeekCompound();
	m_TryToken = tryToken;
	startLocation = tryToken.GetStartLocation();
	lexer.NextCompoundToken();

	// 2. Parse try body (code block)
	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::OpenCurl)
	{
		m_TryBody = CodeBlockNode::TryParse(state);
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected an opening brace for the try body, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
	}

	// 3. Parse catch clauses
	while (true)
	{
		Token t = lexer.PeekCompound();
		if (t.GetType() != TokenType::Catch)
			break;

		auto clause = CreateRef<CatchClauseNode>();
		clause->startLocation = t.GetStartLocation();
		clause->m_CatchToken = t;
		lexer.NextCompoundToken();

		// Expect '('
		t = lexer.PeekCompound();
		if (t.GetType() == TokenType::OpenParen)
		{
			clause->m_OpenParenToken = t;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(t.GetStartLocation(), "Expected '(' after 'catch'", ErrorSeverity::Error);
		}

		// `catch (...)` catches every exception; otherwise an exception type and an optional name.
		t = lexer.PeekCompound();
		if (t.GetType() == TokenType::Ellipsis)
		{
			clause->m_IsCatchAll = true;
			lexer.NextCompoundToken();
		}
		else
		{
			auto exType = CreateRef<QualTypeNode>();
			exType->ParseBody(state);
			clause->m_ExceptionType = exType;
			t = lexer.PeekCompound();
			if (t.GetType() == TokenType::Identifier)
			{
				clause->m_ExceptionNameToken = t;
				lexer.NextCompoundToken();
			}
		}

		// Expect ')'
		t = lexer.PeekCompound();
		if (t.GetType() == TokenType::CloseParen)
		{
			clause->m_CloseParenToken = t;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(t.GetStartLocation(), "Expected ')' after catch parameter", ErrorSeverity::Error);
		}

		// Parse catch body
		t = lexer.PeekCompound();
		if (t.GetType() == TokenType::OpenCurl)
		{
			clause->m_Body = CodeBlockNode::TryParse(state);
		}
		else
		{
			clause->m_SingleStatementBody = ParseAnyStatement(state);
		}

		if (clause->m_Body)
			clause->endLocation = clause->m_Body->endLocation;
		else if (clause->m_SingleStatementBody)
			clause->endLocation = clause->m_SingleStatementBody->endLocation;
		else
			clause->endLocation = clause->startLocation;

		clause->m_Children.Clear();
		if (clause->m_ExceptionType)
			clause->m_Children.Push(clause->m_ExceptionType);
		if (clause->m_Body)
			clause->m_Children.Push(clause->m_Body);
		if (clause->m_SingleStatementBody)
			clause->m_Children.Push(clause->m_SingleStatementBody);

		m_CatchClauses.Push(clause);
	}

	// 4. Optional finally block
	next = lexer.PeekCompound();
	if (next.GetType() == TokenType::Finally)
	{
		lexer.NextCompoundToken(); // consume 'finally'
		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenCurl)
		{
			m_FinallyBody = CodeBlockNode::TryParse(state);
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected an opening brace for the finally body, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
		}
	}

	if (m_FinallyBody)
		endLocation = m_FinallyBody->endLocation;
	else if (!m_CatchClauses.Empty())
		endLocation = m_CatchClauses[m_CatchClauses.Count() - 1]->endLocation;
	else if (m_TryBody)
		endLocation = m_TryBody->endLocation;
	else
		endLocation = startLocation;

	m_Children.Clear();
	if (m_TryBody)
		m_Children.Push(m_TryBody);
	for (uint32 i = 0; i < m_CatchClauses.Count(); ++i)
		m_Children.Push(m_CatchClauses[i]);
	if (m_FinallyBody)
		m_Children.Push(m_FinallyBody);
}

} // namespace Scripting
