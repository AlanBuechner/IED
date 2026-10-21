#include "ForLoopNode.h"
#include "CodeBlockNode.h"
#include "StatementParser.h"
#include "../Declarations/VariableDefinitionNode.h"
#include "../Declarations/NamespaceQualifiedNameNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> ForLoopNode::Children() const
	{
		return m_Children;
	}

	// A named type or a signature type (`func<void()>`, `funcptr<int32(int32)>`, BUG-312), as a loop variable's type is.
	static bool NamesAType(const QualTypeNode& type)
	{
		return (type.BaseName() && !type.BaseName()->GetSegments().Empty()) || type.FuncPtrType();
	}

	Ref<ForLoopNode> ForLoopNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token t = lexer.PeekCompound();
		if (t.GetType() != TokenType::For)
			return nullptr;

		auto node = CreateRef<ForLoopNode>();
		node->ParseBody(state);
		return node;
	}

	void ForLoopNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_InitClause = nullptr;
		m_Condition = nullptr;
		m_Steps.Clear();
		m_Body = nullptr;
		m_SingleStatementBody = nullptr;
		m_IsRangeBased = false;

		// 1. Consume 'for'
		Token forToken = lexer.PeekCompound();
		m_ForToken = forToken;
		startLocation = forToken.GetStartLocation();
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
			errorTracker.AddError(openParen.GetStartLocation(), "Expected '(' after 'for'", ErrorSeverity::Error);
		}

		// 3. Check for range-based for: for (Type name : expr). Detect by parsing a type,
		//    then an identifier, then a ':' - all within one undo scope so a failed probe
		//    leaves the lexer exactly where classic-for parsing expects it.
		Token first = lexer.PeekCompound();
		if (first.GetType() == TokenType::Identifier || first.GetType() == TokenType::Const || first.GetType() == TokenType::FunPtr)
		{
			auto undo = LexerUndoScope(lexer);

			auto varType = QualTypeNode::TryParse(state);
			Token afterType = lexer.PeekCompound();
			if (varType && NamesAType(*varType) && afterType.GetType() == TokenType::Identifier)
			{
				lexer.NextCompoundToken(); // consume the loop variable name
				Token afterName = lexer.PeekCompound();
				if (afterName.GetType() == TokenType::Colon)
				{
					m_IsRangeBased = true;
					m_InitClause = varType;
					m_RangeVarNameToken = afterType;

					Token colon = lexer.PeekCompound();
					m_ColonToken = colon;
					lexer.NextCompoundToken();

					m_Condition = PrattParser::ParseExpression(state);

					Token closeParen = lexer.PeekCompound();
					if (closeParen.GetType() == TokenType::CloseParen)
					{
						m_CloseParenToken = closeParen;
						lexer.NextCompoundToken();
					}
					else
					{
						errorTracker.AddError(closeParen.GetStartLocation(), "Expected ')' after range-based for", ErrorSeverity::Error);
					}

					Token next = lexer.PeekCompound();
					if (next.GetType() == TokenType::OpenCurl)
						m_Body = CodeBlockNode::TryParse(state);
					else
						m_SingleStatementBody = ParseAnyStatement(state);

					if (m_Body)
						endLocation = m_Body->endLocation;
					else if (m_SingleStatementBody)
						endLocation = m_SingleStatementBody->endLocation;
					else
						endLocation = startLocation;

					m_Children.Clear();
					if (m_InitClause)
						m_Children.Push(m_InitClause);
					if (m_Condition)
						m_Children.Push(m_Condition);
					if (m_Body)
						m_Children.Push(m_Body);
					if (m_SingleStatementBody)
						m_Children.Push(m_SingleStatementBody);

					undo.Commit();
					return;
				}
			}

			// Not range-based - roll back the probe and fall through to classic parsing.
			undo.Cancel();
		}

		// Regular for loop parsing
		// 3. Parse init clause (empty, variable def, or expression)
		Token afterOpen = lexer.PeekCompound();
		if (afterOpen.GetType() != TokenType::SemiColon)
		{
			// A for-init variable definition must NOT consume the loop's first ';' - that
			// terminator belongs to this construct. Pass consumeSemicolon=false so the
			// var-def leaves it in place for step 4 below.
			auto varDef = VariableDefinitionNode::TryParse(state, /*consumeSemicolon=*/false);
			if (varDef)
				m_InitClause = varDef;
			else
			{
				auto expr = PrattParser::ParseExpression(state);
				if (expr)
					m_InitClause = expr;
			}
		}

		// 4. Expect first ';'
		Token semi1 = lexer.PeekCompound();
		if (semi1.GetType() == TokenType::SemiColon)
		{
			m_FirstSemicolon = semi1;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(semi1.GetStartLocation(), "Expected ';' in for loop", ErrorSeverity::Error);
		}

		// 5. Parse condition (empty or expression)
		Token condToken = lexer.PeekCompound();
		if (condToken.GetType() != TokenType::SemiColon)
		{
			m_Condition = PrattParser::ParseExpression(state);
		}

		// 6. Expect second ';'
		Token semi2 = lexer.PeekCompound();
		if (semi2.GetType() == TokenType::SemiColon)
		{
			m_SecondSemicolon = semi2;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(semi2.GetStartLocation(), "Expected ';' in for loop", ErrorSeverity::Error);
		}

		// 7. Parse the steps: empty, or expressions separated by commas
		if (lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			m_Steps.Push(PrattParser::ParseExpression(state));
			while (lexer.PeekCompound().GetType() == TokenType::Comma)
			{
				lexer.NextCompoundToken();
				m_Steps.Push(PrattParser::ParseExpression(state));
			}
		}

		// 8. Expect ')'
		Token closeParen = lexer.PeekCompound();
		if (closeParen.GetType() == TokenType::CloseParen)
		{
			m_CloseParenToken = closeParen;
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(closeParen.GetStartLocation(), "Expected ')' after for loop header", ErrorSeverity::Error);
		}

		// 9. Parse body
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
		if (m_InitClause)
			m_Children.Push(m_InitClause);
		if (m_Condition)
			m_Children.Push(m_Condition);
		for (const auto& step : m_Steps)
			if (step) m_Children.Push(step);
		if (m_Body)
			m_Children.Push(m_Body);
		if (m_SingleStatementBody)
			m_Children.Push(m_SingleStatementBody);
	}

} // namespace Scripting
