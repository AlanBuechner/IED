#include "SimpleStatementNodes.h"
#include "CodeBlockNode.h"
#include "StatementParser.h"
#include "ExpressionStatementNode.h"
#include "Parser/PrattParser.h"
#include "../Expressions/BracedInitializerNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{

// ====================================================================
// DoWhileLoopNode
// ====================================================================

std::span<const Ref<AstNode>> DoWhileLoopNode::Children() const
{
	return m_Children;
}

Ref<DoWhileLoopNode> DoWhileLoopNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Do)
		return nullptr;

	auto node = CreateRef<DoWhileLoopNode>();
	node->ParseBody(state);
	return node;
}

void DoWhileLoopNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_Body = nullptr;
	m_SingleStatementBody = nullptr;

	Token doToken = lexer.PeekCompound();
	m_DoToken = doToken;
	startLocation = doToken.GetStartLocation();
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::OpenCurl)
	{
		m_Body = CodeBlockNode::TryParse(state);
	}
	else
	{
		m_SingleStatementBody = ParseAnyStatement(state);
	}

	next = lexer.PeekCompound();
	if (next.GetType() == TokenType::While)
	{
		m_WhileToken = next;
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected 'while' after do-body, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
	}

	next = lexer.PeekCompound();
	if (next.GetType() == TokenType::OpenParen)
	{
		m_OpenParenToken = next;
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(next.GetStartLocation(), "Expected '(' after 'while' in do-while", ErrorSeverity::Error);
	}

	m_Condition = PrattParser::ParseExpression(state);

	next = lexer.PeekCompound();
	if (next.GetType() == TokenType::CloseParen)
	{
		m_CloseParenToken = next;
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(next.GetStartLocation(), "Expected ')' after do-while condition", ErrorSeverity::Error);
	}

	next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after do-while, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}

	if (endLocation.line == 0)
		endLocation = startLocation;

	m_Children.Clear();
	if (m_Condition)
		m_Children.Push(m_Condition);
	if (m_Body)
		m_Children.Push(m_Body);
	if (m_SingleStatementBody)
		m_Children.Push(m_SingleStatementBody);
}


// ====================================================================
// ReturnStatementNode
// ====================================================================

Ref<ReturnStatementNode> ReturnStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Return)
		return nullptr;

	auto node = CreateRef<ReturnStatementNode>();
	node->ParseBody(state);
	return node;
}

void ReturnStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_Expression = nullptr;
	m_BracedInitializer = nullptr;

	Token returnToken = lexer.PeekCompound();
	m_ReturnToken = returnToken;
	startLocation = returnToken.GetStartLocation();
	endLocation = returnToken.GetEndLocation();
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() != TokenType::SemiColon)
	{
		m_Expression = PrattParser::ParseExpression(state);
		if (m_Expression)
			endLocation = m_Expression->endLocation;

		// Handle braced initializer: return Type{x, y};
		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenCurl)
		{
			auto braceInit = CreateRef<BracedInitializerNode>();
			braceInit->ParseBody(state);
			m_BracedInitializer = braceInit;
			endLocation = braceInit->endLocation;
		}
	}

	next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after return, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
	}

	m_Children.Clear();
	if (m_Expression)
		m_Children.Push(m_Expression);
	if (m_BracedInitializer)
		m_Children.Push(m_BracedInitializer);
}

// ====================================================================
// BreakStatementNode
// ====================================================================

Ref<BreakStatementNode> BreakStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Break)
		return nullptr;

	auto node = CreateRef<BreakStatementNode>();
	node->ParseBody(state);
	return node;
}

void BreakStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_BreakToken = lexer.PeekCompound();
	startLocation = m_BreakToken.GetStartLocation();
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after break, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}
}

// ====================================================================
// ContinueStatementNode
// ====================================================================

Ref<ContinueStatementNode> ContinueStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Continue)
		return nullptr;

	auto node = CreateRef<ContinueStatementNode>();
	node->ParseBody(state);
	return node;
}

void ContinueStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_ContinueToken = lexer.PeekCompound();
	startLocation = m_ContinueToken.GetStartLocation();
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after continue, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}
}

// ====================================================================
// DeferStatementNode
// ====================================================================

std::span<const Ref<AstNode>> DeferStatementNode::Children() const
{
	return m_Children;
}

Ref<DeferStatementNode> DeferStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Defer)
		return nullptr;

	auto node = CreateRef<DeferStatementNode>();
	node->ParseBody(state);
	return node;
}

void DeferStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_Body = nullptr;
	m_Expression = nullptr;

	Token deferToken = lexer.PeekCompound();
	m_DeferToken = deferToken;
	startLocation = deferToken.GetStartLocation();
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::OpenCurl)
	{
		m_Body = CodeBlockNode::TryParse(state);
		endLocation = m_Body->endLocation;
	}
	else
	{
		// Parse as expression followed by ';' (handles delete, function calls, etc.)
		m_Expression = PrattParser::ParseExpression(state);
		if (m_Expression)
			endLocation = m_Expression->endLocation;

		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::SemiColon)
		{
			lexer.NextCompoundToken();
		}
		else if (!m_Expression)
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected expression or block after 'defer', got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			endLocation = startLocation;
		}
	}

	m_Children.Clear();
	if (m_Body)
		m_Children.Push(m_Body);
	if (m_Expression)
		m_Children.Push(m_Expression);
}

// ====================================================================
// FallthroughStatementNode
// ====================================================================

Ref<FallthroughStatementNode> FallthroughStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Fallthrough)
		return nullptr;

	auto node = CreateRef<FallthroughStatementNode>();
	node->ParseBody(state);
	return node;
}

void FallthroughStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_FallthroughToken = lexer.PeekCompound();
	startLocation = m_FallthroughToken.GetStartLocation();
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after fallthrough, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}
}

// ====================================================================
// GotoStatementNode
// ====================================================================

Ref<GotoStatementNode> GotoStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Goto)
		return nullptr;

	auto node = CreateRef<GotoStatementNode>();
	node->ParseBody(state);
	return node;
}

void GotoStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	Token gotoToken = lexer.PeekCompound();
	m_GotoToken = gotoToken;
	startLocation = gotoToken.GetStartLocation();
	lexer.NextCompoundToken();

	Token labelName = lexer.PeekCompound();
	if (labelName.GetType() == TokenType::Identifier)
	{
		m_LabelNameToken = labelName;
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			labelName.GetStartLocation(),
			Core::StringFormat::Format("Expected label name after 'goto', got '{0}'", labelName.GetText()),
			ErrorSeverity::Error);
	}

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after goto, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}
}

// ====================================================================
// LabelStatementNode
// ====================================================================

std::span<const Ref<AstNode>> LabelStatementNode::Children() const
{
	return m_Children;
}

Ref<LabelStatementNode> LabelStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Identifier)
		return nullptr;

	auto undo = LexerUndoScope(lexer);
	lexer.NextCompoundToken();

	Token next = lexer.PeekCompound();
	if (next.GetType() != TokenType::Colon)
	{
		undo.Cancel();
		return nullptr;
	}

	auto node = CreateRef<LabelStatementNode>();
	node->m_LabelNameToken = t; // Save the label name BEFORE ParseBody runs
	node->ParseBody(state);
	undo.Commit();
	return node;
}

void LabelStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	m_Statement = nullptr;

	startLocation = m_LabelNameToken.GetStartLocation();

	// Identifier was already consumed by TryParse; consume the colon now
	Token colon = lexer.PeekCompound();
	if (colon.GetType() == TokenType::Colon)
	{
		m_ColonToken = colon;
		lexer.NextCompoundToken();
	}

	m_Statement = ParseAnyStatement(state);
	if (m_Statement)
		endLocation = m_Statement->endLocation;
	else
		endLocation = startLocation;

	m_Children.Clear();
	if (m_Statement)
		m_Children.Push(m_Statement);
}

// ====================================================================
// ThrowStatementNode
// ====================================================================

std::span<const Ref<AstNode>> ThrowStatementNode::Children() const
{
	return m_Children;
}

Ref<ThrowStatementNode> ThrowStatementNode::TryParse(ParserState& state)
{
	Lexer& lexer = state.GetLexer();

	Token t = lexer.PeekCompound();
	if (t.GetType() != TokenType::Throw)
		return nullptr;

	auto node = CreateRef<ThrowStatementNode>();
	node->ParseBody(state);
	return node;
}

void ThrowStatementNode::ParseBody(ParserState& state)
{
	Lexer& lexer = state.GetLexer();
	ErrorTracker& errorTracker = state.GetErrorTracker();

	m_Expression = nullptr;

	Token throwToken = lexer.PeekCompound();
	m_ThrowToken = throwToken;
	startLocation = throwToken.GetStartLocation();
	lexer.NextCompoundToken();

	// Bare 'throw;' rethrows the current exception from an enclosing catch block.
	if (lexer.PeekCompound().GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = lexer.PeekCompound();
		endLocation = m_SemicolonToken.GetEndLocation();
		lexer.NextCompoundToken();
		return;
	}

	m_Expression = PrattParser::ParseExpression(state);
	if (!m_Expression)
	{
		errorTracker.AddError(lexer.PeekCompound().GetStartLocation(), "Expected expression after 'throw'", ErrorSeverity::Error);
	}

	Token next = lexer.PeekCompound();
	if (next.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = next;
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else
	{
		errorTracker.AddError(
			next.GetStartLocation(),
			Core::StringFormat::Format("Expected ';' after throw, got '{0}'", next.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}

	m_Children.Clear();
	if (m_Expression)
		m_Children.Push(m_Expression);
}




} // namespace Scripting
