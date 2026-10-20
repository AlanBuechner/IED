#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	class StmtLowerer;
	struct TryCatchNode; struct CatchClauseNode; struct ThrowStatementNode; struct DeferStatementNode;

	/// Exceptions and scope-exit statements: try/catch, throw, defer. A `finally` lowers as a defer that
	/// covers the try statement and its handlers (`{ defer finally-body; try ... catch ... }`), so codegen has
	/// one cleanup mechanism for defer, destructors and finally.
	class ExceptionLowerer
	{
	public:
		ExceptionLowerer(LoweringContext& ctx, StmtLowerer& stmts) : m_Ctx(ctx), m_Stmts(stmts) {}

		QueryResult<void> LowerTryCatch(const TryCatchNode* n, BlockNode* region);
		QueryResult<void> LowerThrow(const ThrowStatementNode* n, BlockNode* region);
		QueryResult<void> LowerDefer(const DeferStatementNode* n, BlockNode* region);

	private:
		QueryResult<void> LowerTryStatement(const TryCatchNode* n, BlockNode* region);
		QueryResult<TryNode::CatchClause> LowerCatchClause(const CatchClauseNode* cc);
		QueryResult<void> BindCaughtValue(const CatchClauseNode* cc, const Type* caught, TryNode::CatchClause& clause);
		QueryResult<void> LowerDeferBody(const AstNode* body, const AstNode* expression, BlockNode* target);

		LoweringContext& m_Ctx;
		StmtLowerer& m_Stmts;
	};
} // namespace Scripting
