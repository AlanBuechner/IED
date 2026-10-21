#include "StatementParser.h"
#include "AsmStatementNode.h"
#include "CodeBlockNode.h"
#include "DestructuringNode.h"
#include "EmptyStatementNode.h"
#include "ExpressionStatementNode.h"
#include "ForLoopNode.h"
#include "IfStatementNode.h"
#include "SimpleStatementNodes.h"
#include "SwitchStatementNode.h"
#include "TryCatchNode.h"
#include "WhileLoopNode.h"
#include "../Declarations/AliasDefinitionNode.h"
#include "../Declarations/EnumDefinitionNode.h"
#include "../Declarations/FunctionDefinitionNode.h"
#include "../Declarations/ObjectDefinitionNode.h"
#include "../Declarations/UnionDefinitionNode.h"
#include "../Declarations/VariableDefinitionNode.h"

namespace Scripting
{
	Ref<AstNode> ParseAnyStatement(ParserState& state)
	{
#define TRY_PARSE_STATEMENT(type) \
		{ auto node = type::TryParse(state); \
		if (node != nullptr) return node; }

		// Keyword statements first: each starts with a token of its own.
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

		// Structural statements.
		TRY_PARSE_STATEMENT(LabelStatementNode);     // identifier: statement
		TRY_PARSE_STATEMENT(CodeBlockNode);          // standalone scope {...}
		TRY_PARSE_STATEMENT(EmptyStatementNode);     // bare semicolon
		TRY_PARSE_STATEMENT(DestructuringNode);      // [a, float b] = F(); - before an annotated declaration's `[`

		// Declarations, after the keywords so an identifier like 'goto' is not taken for a type.
		TRY_PARSE_STATEMENT(VariableDefinitionNode);
		TRY_PARSE_STATEMENT(FunctionDefinitionNode);
		TRY_PARSE_STATEMENT(ObjectDefinitionNode);
		TRY_PARSE_STATEMENT(EnumDefinitionNode);
		TRY_PARSE_STATEMENT(UnionDefinitionNode);
		TRY_PARSE_STATEMENT(AliasDefinitionNode);

		// Anything else is an expression statement.
		TRY_PARSE_STATEMENT(ExpressionStatementNode);

#undef TRY_PARSE_STATEMENT
		return nullptr;
	}
}
