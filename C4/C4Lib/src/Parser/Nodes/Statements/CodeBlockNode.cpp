#include "CodeBlockNode.h"
#include "Parser/UnexpectedToken.h"
#include "AsmStatementNode.h"
#include "DestructuringNode.h"
#include "EmptyStatementNode.h"
#include "ExpressionStatementNode.h"
#include "IfStatementNode.h"
#include "WhileLoopNode.h"
#include "ForLoopNode.h"
#include "SimpleStatementNodes.h"
#include "SwitchStatementNode.h"
#include "TryCatchNode.h"
#include "../Declarations/VariableDefinitionNode.h"
#include "../Declarations/FunctionDefinitionNode.h"
#include "../Declarations/ObjectDefinitionNode.h"
#include "../Declarations/EnumDefinitionNode.h"
#include "../Declarations/UnionDefinitionNode.h"
#include "../Declarations/AliasDefinitionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{

	Ref<CodeBlockNode> CodeBlockNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::OpenCurl)
			return nullptr;

		auto node = CreateRef<CodeBlockNode>();
		state.EnterBlockLevel();
		struct BlockDepthGuard {
			ParserState& m_State;
			~BlockDepthGuard() { m_State.LeaveBlockLevel(); }
		};
		BlockDepthGuard blockGuard{state};
		node->ParseBody(state);
		return node;
	}

	void CodeBlockNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		// Consume opening brace
		Token openBrace = lexer.PeekCompound();
		m_OpenBraceToken = openBrace;
		startLocation = openBrace.GetStartLocation();
		lexer.NextCompoundToken();

		// Guard against stack overflow from pathologically deep block nesting. Blocks are a separate
		// recursive-descent path from expressions (TryParse -> ParseBody -> nested statement -> nested
		// block), so the expression-depth cap does not cover them. The opening brace is already
		// consumed, so instead of unwinding we report one clean error and skip to the matching '}' -
		// this guarantees forward progress (no infinite loop / no cascade) while stopping the recursion.
		if (state.GetBlockDepth() >= ParserState::kMaxBlockDepth) {
			errorTracker.AddError(
				startLocation,
				Core::StringFormat::Format("Block nesting too deep (maximum is {0})", ParserState::kMaxBlockDepth),
				ErrorSeverity::Error);
			int depth = 1;
			for (;;) {
				Token t = lexer.PeekCompound();
				if (t.GetType() == TokenType::EndOfFile) break;
				if (t.GetType() == TokenType::OpenCurl) ++depth;
				else if (t.GetType() == TokenType::CloseCurl) { if (--depth == 0) break; }
				lexer.NextCompoundToken();
			}
			m_CloseBraceToken = lexer.PeekCompound();
			endLocation = m_CloseBraceToken.GetStartLocation();
			if (m_CloseBraceToken.GetType() == TokenType::CloseCurl)
				lexer.NextCompoundToken(); // consume the matching '}'
			return;
		}

		while (true)
		{
			Token token = lexer.PeekCompound();

			// Stop at end of file.
			if (token.GetType() == TokenType::EndOfFile)
				break;

			// Stop at closing brace.
			if (token.GetType() == TokenType::CloseCurl)
				break;

#define TRY_PARSE_STATEMENT(type) \
		{ auto node = type::TryParse(state); \
	if (node != nullptr) { m_Statements.Push(node); continue; } }

	// Keyword-based control flow statements FIRST (they have unique keyword tokens)
		TRY_PARSE_STATEMENT(IfStatementNode);
		TRY_PARSE_STATEMENT(WhileLoopNode);
		TRY_PARSE_STATEMENT(ForLoopNode);
		TRY_PARSE_STATEMENT(DoWhileLoopNode);
		TRY_PARSE_STATEMENT(SwitchStatementNode);
		TRY_PARSE_STATEMENT(TryCatchNode);
		TRY_PARSE_STATEMENT(ReturnStatementNode);
		TRY_PARSE_STATEMENT(BreakStatementNode);
		TRY_PARSE_STATEMENT(ContinueStatementNode);
		TRY_PARSE_STATEMENT(DeferStatementNode);
		TRY_PARSE_STATEMENT(AsmStatementNode);
		TRY_PARSE_STATEMENT(FallthroughStatementNode);
		TRY_PARSE_STATEMENT(GotoStatementNode);
		TRY_PARSE_STATEMENT(ThrowStatementNode);

	// Structural statements
		TRY_PARSE_STATEMENT(LabelStatementNode);     // identifier: statement
		TRY_PARSE_STATEMENT(CodeBlockNode);          // standalone scope {...}
		TRY_PARSE_STATEMENT(EmptyStatementNode);     // bare semicolon
		TRY_PARSE_STATEMENT(DestructuringNode);      // [a, float b] = F(); - before an annotated declaration's `[`

	// Declarations (greedy - match after keywords to avoid consuming identifiers like 'goto')
	TRY_PARSE_STATEMENT(VariableDefinitionNode);
	TRY_PARSE_STATEMENT(FunctionDefinitionNode);
	TRY_PARSE_STATEMENT(ObjectDefinitionNode);
	TRY_PARSE_STATEMENT(EnumDefinitionNode);
	TRY_PARSE_STATEMENT(UnionDefinitionNode);
	TRY_PARSE_STATEMENT(AliasDefinitionNode);

	// Catch-all expression statement (last)
		TRY_PARSE_STATEMENT(ExpressionStatementNode);

#undef TRY_PARSE_STATEMENT

			// No parser matched - report an error, then skip the token.
			errorTracker.AddError(
				token.GetStartLocation(),
				UnexpectedTokenMessage(lexer),
				ErrorSeverity::Error);
			lexer.NextCompoundToken();
		}

		// Consume closing brace
		Token closeBrace = lexer.PeekCompound();
		if (closeBrace.GetType() == TokenType::CloseCurl)
		{
			m_CloseBraceToken = closeBrace;
			endLocation = closeBrace.GetEndLocation();
			lexer.NextCompoundToken();
		}
		else if (closeBrace.GetType() == TokenType::EndOfFile)
		{
			errorTracker.AddError(
				closeBrace.GetStartLocation(),
				"Expected '}' to close block",
				ErrorSeverity::Error);
			endLocation = startLocation; // best effort
		}
		else
		{
			errorTracker.AddError(
				closeBrace.GetStartLocation(),
				Core::StringFormat::Format("Expected '}' to close block, got '{0}'", closeBrace.GetText()),
				ErrorSeverity::Error);
			endLocation = startLocation; // best effort
		}
	}

	Core::String CodeBlockNode::ToString() const
	{
		if (m_Statements.Empty())
			return "{}";

		Core::String result = "{ ";
			for (uint32 i = 0; i < m_Statements.Count(); i++)
		{
			if (i > 0)
				result += " ";
			result += m_Statements[i]->ToString();
		}
		result += " }";
		return result;
	}

} // namespace Scripting
