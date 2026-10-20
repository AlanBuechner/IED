#include "Backend/HIR/ExceptionLowerer.h"
#include "Backend/HIR/StmtLowerer.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Parser/Nodes/Statements/TryCatchNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/ClassHierarchy.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"

namespace Scripting
{
	namespace
	{
		// While a defer body lowers, control may not leave it: break/continue targets outside it, return,
		// goto and labels are rejected (docs/language/09 section 7). Nested defers stack.
		class DeferBodyGuard
		{
		public:
			explicit DeferBodyGuard(LoweringContext& ctx)
				: m_Ctx(ctx), m_Break(ctx.deferBreakFloor), m_Continue(ctx.deferContinueFloor)
			{
				ctx.deferDepth++;
				ctx.deferBreakFloor = ctx.m_Breaks.size();
				ctx.deferContinueFloor = ctx.m_Continues.size();
			}
			~DeferBodyGuard()
			{
				m_Ctx.deferDepth--;
				m_Ctx.deferBreakFloor = m_Break;
				m_Ctx.deferContinueFloor = m_Continue;
			}
		private:
			LoweringContext& m_Ctx;
			size_t m_Break;
			size_t m_Continue;
		};
	}

	QueryResult<void> ExceptionLowerer::LowerTryCatch(const TryCatchNode* n, BlockNode* region)
	{
		const CodeBlockNode* finallyBody = n->GetFinallyBody().Get();
		if (!finallyBody) return LowerTryStatement(n, region);

		// try ... finally F  ==  { defer F; try ... }: the defer covers the try body and its handlers, so F
		// runs on normal exit, on every jump out (return/break/continue/goto) and while an exception unwinds.
		BlockNode* wrapper = m_Ctx.out->NewNode<BlockNode>(nullptr);
		region->stmts.push_back(wrapper);
		RegionScope wrapperScope(m_Ctx, wrapper, "try-finally");
		DeferNode* cleanup = m_Ctx.out->NewNode<DeferNode>();
		wrapper->stmts.push_back(cleanup);
		cleanup->body = m_Stmts.MakeBodyRegion();
		if (auto r = LowerDeferBody(finallyBody, nullptr, cleanup->body); !r.Ok()) return r;
		return LowerTryStatement(n, wrapper);
	}

	QueryResult<void> ExceptionLowerer::LowerTryStatement(const TryCatchNode* n, BlockNode* region)
	{
		TryNode* node = m_Ctx.out->NewNode<TryNode>();
		region->stmts.push_back(node);

		node->tryBlock = m_Stmts.MakeBodyRegion();
		{
			RegionScope tryScope(m_Ctx, node->tryBlock, "try");
			if (auto r = m_Stmts.LowerBody(n->GetTryBody().Get(), nullptr, node->tryBlock); !r.Ok()) return r;
		}

		// Clauses stay in source order: the first clause whose type matches handles the exception.
		for (const auto& c : n->GetCatchClauses())
		{
			auto clause = LowerCatchClause(c.Get());
			if (!clause.Ok()) return QueryResult<void>::Fail(clause.error, clause.message);
			node->catches.push_back(clause.value);
		}
		return QueryResult<void>::Success();
	}

	QueryResult<TryNode::CatchClause> ExceptionLowerer::LowerCatchClause(const CatchClauseNode* cc)
	{
		const LoweringAt at(*m_Ctx.out, cc->startLocation); // the caught value's slot and the handler's entry
		TryNode::CatchClause clause;
		clause.origin = cc->startLocation;
		clause.block = m_Stmts.MakeBodyRegion();
		RegionScope clauseScope(m_Ctx, clause.block, "catch");
		if (cc->IsCatchAll())
			clause.catchAll = true;
		else
		{
			auto et = m_Ctx.Fail(cc, m_Ctx.queries().ResolveTypeCtx(*cc->GetExceptionType().Get(), m_Ctx.unitId, m_Ctx.scope));
			if (!et.Ok()) return QueryResult<TryNode::CatchClause>::Fail(et.error, et.message);
			if (auto bound = BindCaughtValue(cc, et.value, clause); !bound.Ok())
				return QueryResult<TryNode::CatchClause>::Fail(bound.error, bound.message);
		}
		auto r = m_Stmts.LowerBody(cc->GetBody().Get(), cc->GetSingleStatementBody().Get(), clause.block);
		if (!r.Ok()) return QueryResult<TryNode::CatchClause>::Fail(r.error, r.message);
		return clause;
	}

	QueryResult<void> ExceptionLowerer::BindCaughtValue(const CatchClauseNode* cc, const Type* caught, TryNode::CatchClause& clause)
	{
		// The caught value is a slot local to this clause. By value, the runtime copies the exception into it;
		// by reference, it receives the exception object's address and the name reads through it.
		clause.type = caught;
		clause.byReference = caught->GetKind() == TypeKind::Reference;
		const Type* slotType = caught;
		bool refersToConst = false;
		if (clause.byReference)
		{
			const auto* ref = static_cast<const ReferenceType*>(caught);
			clause.type = TypeHandle::UnwrapToValue(ref->GetReferentType());
			slotType = m_Ctx.queries().TypeStore()->Pointer(clause.type);
			refersToConst = ref->IsConstReferent();
			clause.constQualified = refersToConst;
		}
		else if (const Type* value = TypeHandle::UnwrapToValue(caught); value && value->GetKind() == TypeKind::Pointer)
			clause.constQualified = static_cast<const PointerType*>(value)->IsConstReferent();
		const Core::String name = cc->HasExceptionName() ? cc->GetExceptionNameToken().GetText() : Core::String("__caught");
		clause.slot = m_Ctx.AllocSlot(slotType, name, SlotClass::Local);
		// A by-value catch variable is a copy of the exception object: the type must be copyable, and the
		// copy is destroyed when the handler ends.
		if (!clause.byReference)
		{
			if (auto copyable = LifecycleFacts::CheckCopyable(m_Ctx.queries(), slotType); !copyable.Ok())
				return m_Ctx.Fail(cc, QueryResult<void>::Fail(copyable.error, copyable.message + " (catch it by reference)"));
			LValue lv; lv.kind = LValue::Kind::Slot; lv.type = TypeHandle::UnwrapToValue(slotType); lv.slot = clause.slot;
			m_Ctx.NoteCleanup(clause.slot, Lifecycle(m_Ctx).ScheduleDestroy(clause.block, m_Ctx.NewLValue(lv)), clause.block);
		}
		if (!cc->HasExceptionName()) return QueryResult<void>::Success();
		SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.type = slotType; sym.definingUnit = 0;
		sym.isConst = cc->GetExceptionType() && cc->GetExceptionType()->DeclaresConstValue();
		m_Ctx.BindLocal(name, sym, clause.slot, clause.byReference, refersToConst);
		return QueryResult<void>::Success();
	}

	QueryResult<void> ExceptionLowerer::LowerThrow(const ThrowStatementNode* n, BlockNode* region)
	{
		if (n->IsRethrow()) // bare `throw;` rethrows the in-flight exception
		{
			region->stmts.push_back(m_Ctx.out->NewNode<ThrowNode>(nullptr, true));
			return QueryResult<void>::Success();
		}
		// The exception object is a copy of an lvalue, or the fresh value itself (docs/language/06).
		ExprLowerer expr(m_Ctx);
		auto v = expr.ResolveExpr(n->GetExpression().Get());
		if (!v.Ok()) return QueryResult<void>::Fail(v.error, v.message);
		auto value = m_Ctx.Fail(n, InitialValue(m_Ctx, v.value, TypeHandle::UnwrapToValue(v.value.type)));
		if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message);
		auto* node = m_Ctx.out->NewNode<ThrowNode>(value.value, false);
		if (const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(v.value.type)))
		{
			Lifecycle life(m_Ctx);
			node->destroy = life.Destroy(obj);
			for (const auto& s : ClassHierarchy::Subobjects(obj))
				if (!s.ambiguous) node->copies.push_back({ s.type, life.Copy(s.type) });
		}
		region->stmts.push_back(node);
		return QueryResult<void>::Success();
	}

	QueryResult<void> ExceptionLowerer::LowerDefer(const DeferStatementNode* n, BlockNode* region)
	{
		// `defer <expr>;` / `defer { ... }`: runs when the enclosing scope exits, on every exit edge, LIFO
		// with the scope's other defers and value-local destructors. A defer is a scope point for goto.
		if (!n->GetBody() && !n->GetExpression())
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "defer with no body or expression"));
		DeferNode* node = m_Ctx.out->NewNode<DeferNode>();
		region->stmts.push_back(node);
		m_Ctx.jumps.NoteDeclaration("a defer");
		node->body = m_Stmts.MakeBodyRegion();
		return LowerDeferBody(n->GetBody().Get(), n->GetExpression().Get(), node->body);
	}

	QueryResult<void> ExceptionLowerer::LowerDeferBody(const AstNode* body, const AstNode* expression, BlockNode* target)
	{
		DeferBodyGuard guard(m_Ctx);
		RegionScope bodyScope(m_Ctx, target, "defer");
		if (const auto* block = dynamic_cast<const CodeBlockNode*>(body))
			return m_Stmts.LowerBody(block, nullptr, target);
		ExprLowerer expr(m_Ctx);
		auto e = expr.ResolveExpr(expression);
		if (!e.Ok()) return QueryResult<void>::Fail(e.error, e.message);
		target->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.LoadIfNeeded(e.value)));
		return QueryResult<void>::Success();
	}
} // namespace Scripting
