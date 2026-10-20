#include "Backend/HIR/LoopLowerer.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/StmtLowerer.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Parser/Nodes/Statements/WhileLoopNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Model/TypeHandle.h"
#include "Model/Types/InitListType.h"
#include "Model/Types/ReferenceType.h"

namespace Scripting
{
	namespace
	{
		// Pops the innermost loop's break/continue labels on every exit path.
		class LoopLabels
		{
		public:
			LoopLabels(LoweringContext& ctx, uint32& breakL, uint32& contL) : m_Ctx(ctx) { ctx.PushLoop(breakL, contL); }
			~LoopLabels() { m_Ctx.PopLoop(); }
		private:
			LoweringContext& m_Ctx;
		};

		const LValue* SlotLValue(LoweringContext& ctx, uint32 slot, const Type* t)
		{
			LValue lv; lv.kind = LValue::Kind::Slot; lv.type = t; lv.slot = slot;
			return ctx.NewLValue(lv);
		}
	}

	QueryResult<void> LoopLowerer::LowerWhile(const WhileLoopNode* n, BlockNode* region)
	{
		auto cond = m_Stmts.Condition(n->GetCondition().Get());
		if (!cond.Ok()) return QueryResult<void>::Fail(cond.error, cond.message);

		WhileNode* node = m_Ctx.out->NewNode<WhileNode>(cond.value);
		LoopLabels labels(m_Ctx, node->breakLabel, node->continueLabel);
		region->stmts.push_back(node);

		node->body = m_Stmts.MakeBodyRegion();
		RegionScope bodyScope(m_Ctx, node->body, "while-body");
		return m_Stmts.LowerBody(n->GetBody().Get(), n->GetSingleStatementBody().Get(), node->body);
	}

	QueryResult<void> LoopLowerer::LowerDoWhile(const DoWhileLoopNode* n, BlockNode* region)
	{
		// do { body } while (cond): a While whose body runs once before the first test.
		auto cond = m_Stmts.Condition(n->GetCondition().Get());
		if (!cond.Ok()) return QueryResult<void>::Fail(cond.error, cond.message);

		WhileNode* node = m_Ctx.out->NewNode<WhileNode>(cond.value);
		node->isDoWhile = true;
		LoopLabels labels(m_Ctx, node->breakLabel, node->continueLabel);
		region->stmts.push_back(node);

		node->body = m_Stmts.MakeBodyRegion();
		RegionScope bodyScope(m_Ctx, node->body, "do-body");
		return m_Stmts.LowerBody(n->GetBody().Get(), n->GetSingleStatementBody().Get(), node->body);
	}

	QueryResult<void> LoopLowerer::LowerFor(const ForLoopNode* n, BlockNode* region)
	{
		if (n->IsRangeBased()) return LowerRangeFor(n, region);

		// for (init; cond; step) body: the init block is the scope of the whole statement - init locals are
		// visible in cond, body and step, and are destroyed when the loop ends, not when the init clause ends.
		ForNode* node = m_Ctx.out->NewNode<ForNode>();
		region->stmts.push_back(node);
		node->initBlock = m_Ctx.out->NewNode<BlockNode>(nullptr);
		RegionScope forScope(m_Ctx, node->initBlock, "for");
		if (auto r = LowerForInit(n, node->initBlock); !r.Ok()) return r;

		LoopLabels labels(m_Ctx, node->breakLabel, node->continueLabel);
		if (n->GetCondition())
		{
			auto c = m_Stmts.Condition(n->GetCondition().Get());
			if (!c.Ok()) return QueryResult<void>::Fail(c.error, c.message);
			node->cond = c.value;
		}

		node->body = m_Stmts.MakeBodyRegion();
		{
			RegionScope bodyScope(m_Ctx, node->body, "for-body");
			if (auto r = m_Stmts.LowerBody(n->GetBody().Get(), n->GetSingleStatementBody().Get(), node->body); !r.Ok()) return r;
		}

		return LowerSteps(n, node);
	}

	// The step clause runs its expressions in order, each a full expression of its own (D-40).
	QueryResult<void> LoopLowerer::LowerSteps(const ForLoopNode* n, ForNode* node)
	{
		std::vector<const HIRNode*> steps;
		for (const auto& step : n->GetSteps())
		{
			auto lowered = m_Stmts.FullExpression(step.Get());
			if (!lowered.Ok()) return QueryResult<void>::Fail(lowered.error, lowered.message);
			steps.push_back(lowered.value);
		}
		if (steps.size() == 1) node->step = steps[0];
		else if (!steps.empty())
		{
			auto* sequence = m_Ctx.out->NewNode<SequenceNode>(steps.back()->type);
			sequence->steps = steps;
			node->step = sequence;
		}
		return QueryResult<void>::Success();
	}

	QueryResult<void> LoopLowerer::LowerForInit(const ForLoopNode* n, BlockNode* initBlock)
	{
		if (!n->GetInitClause()) return QueryResult<void>::Success();
		if (const auto* vd = dynamic_cast<const VariableDefinitionNode*>(n->GetInitClause().Get()))
		{
			const size_t mark = m_Ctx.pendingTemporaries.size();
			if (auto r = DeclLowerer(m_Ctx).LowerLocalDecl(vd, initBlock); !r.Ok()) return r;
			Lifecycle(m_Ctx).EndTemporaries(initBlock, mark);
			return QueryResult<void>::Success();
		}
		auto e = m_Stmts.FullExpression(n->GetInitClause().Get());
		if (!e.Ok()) return QueryResult<void>::Fail(e.error, e.message);
		initBlock->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(e.value));
		return QueryResult<void>::Success();
	}

	QueryResult<void> LoopLowerer::LowerLoopBody(const ForLoopNode* n, BlockNode* body)
	{
		return m_Stmts.LowerBody(n->GetBody().Get(), n->GetSingleStatementBody().Get(), body);
	}

	QueryResult<void> LoopLowerer::DeclareLoopVariable(const ForLoopNode* n, const Type* t, const Value& element, BlockNode* body)
	{
		if (const auto* ref = dynamic_cast<const ReferenceType*>(t)) return DeclareLoopReference(n, ref, element, body);
		auto initial = InitialValue(m_Ctx, element, t);
		if (!initial.Ok()) return m_Ctx.Fail(n, QueryResult<void>::Fail(initial.error, initial.message));
		const auto* declared = dynamic_cast<const QualTypeNode*>(n->GetInitClause().Get());
		uint32 slot = DeclareHidden(body, t, n->GetRangeVarNameToken().GetText(), initial.value, declared && declared->DeclaresConstValue());
		Lifecycle(m_Ctx).ScheduleDestroy(body, SlotLValue(m_Ctx, slot, t));
		return QueryResult<void>::Success();
	}

	// D-54: `for (T& v : range)` names each element itself: the slot holds the element's address, as a reference
	// local's does. The element must be a location of type T; a `const T&` may alias a read-only one.
	QueryResult<void> LoopLowerer::DeclareLoopReference(const ForLoopNode* n, const ReferenceType* ref, const Value& element, BlockNode* body)
	{
		const Core::String name = n->GetRangeVarNameToken().GetText();
		const Type* referent = TypeHandle::UnwrapToValue(ref->GetReferentType());
		const Type* elementType = TypeHandle::UnwrapToValue(element.type);
		auto fail = [&](const Core::String& why) { return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, why)); };
		if (!element.isLValue)
			return fail("'" + name + "' is a reference, but each element is a copy (the iterator's 'dereference' returns '" +
				Core::String(elementType ? elementType->GetName() : "?") + "'): return a reference from it, or declare '" +
				referent->GetName() + " " + name + "'");
		if (elementType != referent)
			return fail("'" + name + "' is a '" + ref->GetName() + "', but the elements are '" + elementType->GetName() + "'");
		if (!ref->IsConstReferent() && IsReadOnly(m_Ctx, element.lvalue))
			return fail("the elements are read-only: declare 'const " + referent->GetName() + "& " + name + "'");
		const Type* slotType = m_Ctx.queries().TypeStore()->Pointer(referent);
		const uint32 slot = m_Ctx.AllocSlot(slotType, name, SlotClass::Local);
		SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.type = slotType; sym.definingUnit = 0;
		m_Ctx.BindLocal(name, sym, slot, true, ref->IsConstReferent());
		auto* decl = m_Ctx.out->NewNode<LocalDeclNode>(slot, slotType, m_Ctx.AddressAs(slotType, element.lvalue));
		decl->m_Name = name;
		body->stmts.push_back(decl);
		return QueryResult<void>::Success();
	}

	uint32 LoopLowerer::DeclareHidden(BlockNode* initBlock, const Type* t, const Core::String& name, const HIRNode* init, bool isConst)
	{
		uint32 slot = m_Ctx.AllocSlot(t, name, SlotClass::Local);
		SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.type = t; sym.definingUnit = 0; sym.isConst = isConst;
		m_Ctx.BindLocal(name, sym, slot);
		auto* decl = m_Ctx.out->NewNode<LocalDeclNode>(slot, t, init);
		decl->m_Name = name;
		initBlock->stmts.push_back(decl);
		return slot;
	}

	QueryResult<void> LoopLowerer::LowerRangeFor(const ForLoopNode* n, BlockNode* region)
	{
		// for (Type v : range) over an array, a primitive pointer or an __init_list<T>:
		//   int64 __range_i = 0; for (; __range_i < N; __range_i += 1) { Type v = range[__range_i]; body }
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const auto* varTypeNode = dynamic_cast<const QualTypeNode*>(n->GetInitClause().Get());
		if (!varTypeNode) return QueryResult<void>::Fail(QueryError::Unsupported, "range-for has no type clause");
		auto vt = m_Ctx.queries().ResolveTypeCtx(*varTypeNode, m_Ctx.unitId, m_Ctx.scope);
		if (!vt.Ok()) return QueryResult<void>::Fail(vt.error, vt.message);

		ExprLowerer expr(m_Ctx);
		auto range = expr.ResolveExpr(n->GetRangeExpression().Get());
		if (!range.Ok()) return QueryResult<void>::Fail(range.error, range.message);

		const Type* baseType = TypeHandle::UnwrapToValue(range.value.type);
		const Type* elemType = nullptr; size_t elemSize = 0; uint64 count = 0;
		const InitListType* il = nullptr;
		if (const auto* ar = dynamic_cast<const ArrayType*>(baseType))
		{
			elemType = ar->GetElementType(); elemSize = elemType ? elemType->GetSize() : 0; count = ar->GetElementCount();
		}
		else if (dynamic_cast<const PointerType*>(baseType))
		{
			elemType = m_Ctx.queries().ElementOf(baseType);
			if (!elemType) return QueryResult<void>::Fail(QueryError::NotAValue, "cannot range-for over this pointer");
			// A pointer to an OBJECT is a handle to one object: it iterates through the protocol.
			if (elemType->GetKind() == TypeKind::Object) return LowerRangeForIterator(n, region);
			elemSize = elemType->GetSize();
		}
		else if (baseType->GetKind() == TypeKind::InitList)
		{
			il = static_cast<const InitListType*>(baseType);
			elemType = il->GetElementType(); elemSize = elemType ? elemType->GetSize() : 0;
		}
		else
			return LowerRangeForIterator(n, region);

		const Type* intT = ts->Builtin(TypeKind::Int64);
		ForNode* node = m_Ctx.out->NewNode<ForNode>();
		region->stmts.push_back(node);
		node->initBlock = m_Ctx.out->NewNode<BlockNode>(nullptr);
		RegionScope rangeScope(m_Ctx, node->initBlock, "range-for");
		LoopLabels labels(m_Ctx, node->breakLabel, node->continueLabel);

		uint32 idxSlot = DeclareHidden(node->initBlock, intT, "__range_i", m_Ctx.out->NewNode<ConstantNode>(intT, ConstantValue::Int(0)));

		// An __init_list<T> range is bounded by its own members: hoist data and size into hidden locals once.
		uint32 dataSlot = 0, nSlot = 0;
		const Type* ptrToElem = nullptr;
		if (il)
		{
			ptrToElem = ts->Pointer(elemType);
			LValue dataLv; dataLv.kind = LValue::Kind::Field; dataLv.type = ptrToElem;
			dataLv.base = range.value.isLValue ? range.value.lvalue : nullptr;
			dataLv.field = il->FindMember("data");
			dataSlot = DeclareHidden(node->initBlock, ptrToElem, "__range_data", m_Ctx.out->NewNode<LoadNode>(ptrToElem, m_Ctx.NewLValue(dataLv)));

			const Type* sizeType = il->FindMember("size")->m_Type;
			LValue sizeLv; sizeLv.kind = LValue::Kind::Field; sizeLv.type = sizeType;
			sizeLv.base = range.value.isLValue ? range.value.lvalue : nullptr;
			sizeLv.field = il->FindMember("size");
			const HIRNode* szRaw = m_Ctx.out->NewNode<LoadNode>(sizeType, m_Ctx.NewLValue(sizeLv));
			const HIRNode* szAsInt = (sizeType != intT) ? m_Ctx.out->NewNode<ConvertNode>(szRaw, sizeType, intT) : szRaw;
			nSlot = DeclareHidden(node->initBlock, intT, "__range_n", szAsInt);
		}

		const Type* boolT = ts->Builtin(TypeKind::Bool);
		const HIRNode* idxLoad = m_Ctx.out->NewNode<LoadNode>(intT, SlotLValue(m_Ctx, idxSlot, intT));
		if (count > 0)
			node->cond = m_Ctx.out->NewNode<IntrinsicOpNode>(boolT, IntrinsicOp::Lt, idxLoad,
				m_Ctx.out->NewNode<ConstantNode>(intT, ConstantValue::Int((int64)count)));
		else if (il)
			node->cond = m_Ctx.out->NewNode<IntrinsicOpNode>(boolT, IntrinsicOp::Lt, idxLoad,
				m_Ctx.out->NewNode<LoadNode>(intT, SlotLValue(m_Ctx, nSlot, intT)));

		// body: T v = range[__range_i] (a copy, destroyed at the end of each iteration), then the user's statements.
		node->body = m_Stmts.MakeBodyRegion();
		{
			RegionScope bodyScope(m_Ctx, node->body, "range-for-body");
			const HIRNode* bodyIdx = m_Ctx.out->NewNode<LoadNode>(intT, SlotLValue(m_Ctx, idxSlot, intT));
			const LValue* elemLoc = il
				? BuildIndexLValue(m_Ctx, Value::L(SlotLValue(m_Ctx, dataSlot, ptrToElem)), bodyIdx, elemType, elemSize)
				: BuildIndexLValue(m_Ctx, range.value, bodyIdx, elemType, elemSize);
			if (auto r = DeclareLoopVariable(n, vt.value, Value::L(elemLoc), node->body); !r.Ok()) return r;
			if (auto r = LowerLoopBody(n, node->body); !r.Ok()) return r;
		}

		// step: __range_i += 1.
		const HIRNode* cur = m_Ctx.out->NewNode<LoadNode>(intT, SlotLValue(m_Ctx, idxSlot, intT));
		const HIRNode* next = m_Ctx.out->NewNode<IntrinsicOpNode>(intT, IntrinsicOp::Add, cur,
			m_Ctx.out->NewNode<ConstantNode>(intT, ConstantValue::Int(1)));
		node->step = m_Ctx.out->NewNode<StoreNode>(SlotLValue(m_Ctx, idxSlot, intT), next);
		return QueryResult<void>::Success();
	}

	QueryResult<void> LoopLowerer::LowerRangeForIterator(const ForLoopNode* n, BlockNode* region)
	{
		// for (Type v : range) over the iterator protocol:
		//   it = range.begin(); end = range.end();
		//   for (; it notequal end; it = increment(it)) { Type v = dereference(it); body }
		// so `continue` runs the increment and `break` skips it. Every operator resolves through QueryOperator.
		const auto* varTypeNode = dynamic_cast<const QualTypeNode*>(n->GetInitClause().Get());
		if (!varTypeNode) return QueryResult<void>::Fail(QueryError::Unsupported, "range-for has no type clause");
		auto vt = m_Ctx.queries().ResolveTypeCtx(*varTypeNode, m_Ctx.unitId, m_Ctx.scope);
		if (!vt.Ok()) return QueryResult<void>::Fail(vt.error, vt.message);

		ExprLowerer expr(m_Ctx);
		auto range = expr.ResolveExpr(n->GetRangeExpression().Get());
		if (!range.Ok()) return QueryResult<void>::Fail(range.error, range.message);
		const Type* collType = TypeHandle::UnwrapToValue(range.value.type);
		// A pointer range is a handle to one object: the protocol operators are looked up on the pointee
		// while the receiver stays the pointer (codegen derefs it, like `v->Begin()`).
		const Type* opType = collType;
		if (collType->GetKind() == TypeKind::Pointer)
		{
			const Type* pointee = static_cast<const PointerType*>(collType)->GetPointeeType();
			if (!pointee || pointee->GetKind() != TypeKind::Object)
				return QueryResult<void>::Fail(QueryError::NotAValue, "cannot range-for over this pointer");
			opType = pointee;
		}

		// begin / end are asked of the range itself, so a read-only range needs const ones (D-31).
		const bool readOnlyRange = collType->GetKind() == TypeKind::Pointer ? static_cast<const PointerType*>(collType)->IsConstReferent()
			: range.value.isLValue && IsReadOnly(m_Ctx, range.value.lvalue);
		auto protocolRecord = [&](const char* op, const Type* on, const Type* rhs, const FunctionRecord*& out) -> QueryResult<void> {
			const bool onRange = Core::String(op) == "begin" || Core::String(op) == "end";
			auto b = m_Ctx.queries().QueryOperator(op, on, rhs, m_Ctx.unitId, m_Ctx.enclosingType,
				onRange && readOnlyRange ? ReceiverAccess::ReadOnly : ReceiverAccess::Writable);
			if (!b.Ok()) return QueryResult<void>::Fail(b.error, b.message);
			out = b.value.record;
			if (!out) return QueryResult<void>::Fail(QueryError::NotFound, Core::String("iterator protocol: '") + op + "' has no body");
			return AccessCheck::CheckCall(m_Ctx, *out);
		};
		const FunctionRecord *beginRec = nullptr, *endRec = nullptr, *neqRec = nullptr, *derefRec = nullptr, *incRec = nullptr;
		if (auto r = protocolRecord("begin", opType, nullptr, beginRec); !r.Ok()) return r;
		const Type* itType = TypeHandle::UnwrapToValue(beginRec->type->GetReturnType());
		if (auto r = protocolRecord("end", opType, nullptr, endRec); !r.Ok()) return r;
		if (auto r = protocolRecord("notequal", itType, itType, neqRec); !r.Ok()) return r;
		if (auto r = protocolRecord("dereference", itType, nullptr, derefRec); !r.Ok()) return r;
		if (auto r = protocolRecord("increment", itType, nullptr, incRec); !r.Ok()) return r;

		ForNode* node = m_Ctx.out->NewNode<ForNode>();
		region->stmts.push_back(node);
		node->initBlock = m_Ctx.out->NewNode<BlockNode>(nullptr);
		RegionScope rangeScope(m_Ctx, node->initBlock, "range-for-iter");
		LoopLabels labels(m_Ctx, node->breakLabel, node->continueLabel);

		// The collection (`this` of begin/end). An rvalue range is materialized into a per-loop hidden local; a
		// pointer range uses *ptr as the receiver.
		LValue rangeL;
		if (range.value.isLValue)
		{
			rangeL = *range.value.lvalue;
			if (rangeL.type && rangeL.type->GetKind() == TypeKind::Pointer)
			{
				const LValue* slotLv = m_Ctx.NewLValue(rangeL);
				LValue deref; deref.kind = LValue::Kind::Deref;
				deref.type = TypeHandle::UnwrapToValue(static_cast<const PointerType*>(rangeL.type)->GetPointeeType());
				deref.addr = m_Ctx.LoadIfNeeded(Value::L(slotLv));
				rangeL = *m_Ctx.NewLValue(deref);
			}
		}
		else
		{
			uint32 collSlot = DeclareHidden(node->initBlock, collType, "__range_coll", range.value.rvalue);
			rangeL = *SlotLValue(m_Ctx, collSlot, collType);
			Lifecycle(m_Ctx).ScheduleDestroy(node->initBlock, SlotLValue(m_Ctx, collSlot, collType));
		}

		auto protocolCall = [&](const FunctionRecord* rec, const LValue* receiver) {
			auto call = m_Ctx.out->NewNode<CallNode>(rec->type->GetReturnType(), rec);
			call->isOperatorCall = true;
			BindReceiver(m_Ctx, call, receiver);
			return call;
		};
		uint32 itSlot = DeclareHidden(node->initBlock, itType, "__range_it", protocolCall(beginRec, m_Ctx.NewLValue(rangeL)));
		uint32 endSlot = DeclareHidden(node->initBlock, itType, "__range_end", protocolCall(endRec, m_Ctx.NewLValue(rangeL)));
		Lifecycle life(m_Ctx);
		life.ScheduleDestroy(node->initBlock, SlotLValue(m_Ctx, itSlot, itType));
		life.ScheduleDestroy(node->initBlock, SlotLValue(m_Ctx, endSlot, itType));

		auto neqCall = protocolCall(neqRec, SlotLValue(m_Ctx, itSlot, itType));
		neqCall->args.push_back(m_Ctx.LoadIfNeeded(Value::L(SlotLValue(m_Ctx, endSlot, itType))));
		node->cond = neqCall;

		// body: v = dereference(it). A reference-returning dereference yields the referent's address, so the
		// element is loaded through it; a by-value dereference already yields the element.
		node->body = m_Stmts.MakeBodyRegion();
		{
			RegionScope bodyScope(m_Ctx, node->body, "range-for-iter-body");
			auto derefCall = protocolCall(derefRec, SlotLValue(m_Ctx, itSlot, itType));
			const Type* derefRetRaw = derefRec->type->GetReturnType();
			Value element = Value::R(derefCall, TypeHandle::UnwrapToValue(derefRetRaw));
			if (derefRetRaw && derefRetRaw->GetKind() == TypeKind::Reference)
			{
				LValue derefLv; derefLv.kind = LValue::Kind::Deref; derefLv.type = TypeHandle::UnwrapToValue(derefRetRaw); derefLv.addr = derefCall;
				element = Value::L(m_Ctx.NewLValue(derefLv));
			}
			if (auto r = DeclareLoopVariable(n, vt.value, element, node->body); !r.Ok()) return r;
			if (auto r = LowerLoopBody(n, node->body); !r.Ok()) return r;
		}

		// step: it = increment(it) - an assignment, so an iterator with a lifecycle replaces its old value properly.
		const LValue* it = SlotLValue(m_Ctx, itSlot, itType);
		const HIRNode* next = protocolCall(incRec, it);
		if (life.AssignmentNeedsLifecycle(itType))
		{
			auto assigned = life.Assign(it, Value::R(next, itType));
			if (!assigned.Ok()) return m_Ctx.Fail(n, QueryResult<void>::Fail(assigned.error, assigned.message));
			node->step = m_Ctx.out->NewNode<AddressOfNode>(m_Ctx.queries().TypeStore()->Pointer(itType), assigned.value);
		}
		else
			node->step = m_Ctx.out->NewNode<StoreNode>(it, next);
		return QueryResult<void>::Success();
	}
} // namespace Scripting
