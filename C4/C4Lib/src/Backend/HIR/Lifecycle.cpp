#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/IndexLoop.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/EvaluatedOnce.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"

namespace Scripting
{
	const ObjectType* Lifecycle::AsObject(const Type* t)
	{
		t = TypeHandle::UnwrapToValue(t);
		return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
	}

	const ArrayType* Lifecycle::AsArray(const Type* t)
	{
		t = TypeHandle::UnwrapToValue(t);
		return t && t->GetKind() == TypeKind::Array ? static_cast<const ArrayType*>(t) : nullptr;
	}

	// --- the functions -----------------------------------------------------------------------------------

	const FunctionRecord* Lifecycle::Synthesized(const ObjectType* type, LifecycleOp op)
	{
		return m_Ctx.queries().Model()->EnsureSynthesizedLifecycle(type, op);
	}

	const FunctionRecord* Lifecycle::DefaultCreate(const ObjectType* type)
	{
		if (!LifecycleFacts::NeedsConstruct(m_Ctx.queries(), type)) return nullptr;
		LifecycleFacts::OwnMethods own = LifecycleFacts::Own(m_Ctx.queries(), type);
		return own.defaultCreate ? own.defaultCreate : Synthesized(type, LifecycleOp::Create);
	}

	std::vector<const HIRNode*> Lifecycle::DefaultArguments(const FunctionRecord* create)
	{
		std::vector<const HIRNode*> args;
		if (!create || !create->type) return args;
		const auto& params = create->type->GetParameters();
		for (uint32 i = 0; i < params.Count() && i < create->paramDefaults.Count() && create->paramDefaults[i]; ++i)
		{
			auto value = ExprLowerer(m_Ctx).DefaultArgument(*create, i);
			if (!value.Ok()) { m_Ctx.Fail(create->paramDefaults[i], value); break; }
			auto bound = BindArgument(m_Ctx, value.value, m_Ctx.LoadIfNeeded(value.value), params[i].m_Type);
			if (!bound.Ok()) { m_Ctx.Fail(create->paramDefaults[i], bound); break; }
			args.push_back(bound.value);
		}
		return args;
	}

	const FunctionRecord* Lifecycle::Destroy(const ObjectType* type)
	{
		if (!LifecycleFacts::NeedsDestroy(m_Ctx.queries(), type)) return nullptr;
		LifecycleFacts::OwnMethods own = LifecycleFacts::Own(m_Ctx.queries(), type);
		// A linked delete (a C++ destructor) destroys the whole C++ object; a C4 delete gets its fields and
		// base added around its body (ScheduleDestroyParts).
		return own.destroy ? own.destroy : Synthesized(type, LifecycleOp::Delete);
	}

	const FunctionRecord* Lifecycle::Copy(const ObjectType* type)
	{
		if (!LifecycleFacts::NeedsCopy(m_Ctx.queries(), type)) return nullptr;
		LifecycleFacts::OwnMethods own = LifecycleFacts::Own(m_Ctx.queries(), type);
		if (own.copy) return own.copy;
		if (own.assign) return own.assign; // assignment into zeroed storage is a copy
		return Synthesized(type, LifecycleOp::Copy);
	}

	// --- statements --------------------------------------------------------------------------------------

	void Lifecycle::Construct(BlockNode* region, const LValue* object)
	{
		if (const ArrayType* arr = AsArray(object->type))
		{
			if (!LifecycleFacts::NeedsConstruct(m_Ctx.queries(), arr->GetElementType())) return;
			ForEachIndex(region, 0, arr->GetElementCount(), false, [&](BlockNode* body, const HIRNode* i) { Construct(body, ElementOf(object, i)); });
			return;
		}
		const ObjectType* obj = AsObject(object->type);
		if (!obj) return;
		if (const FunctionRecord* create = DefaultCreate(obj))
			region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(DirectCall(create, object, DefaultArguments(create))));
	}

	void Lifecycle::ConstructVariable(BlockNode* region, const LValue* object)
	{
		Construct(AsArray(object->type) ? FillGuard(region, object) : region, object);
	}

	void Lifecycle::ConstructElements(BlockNode* region, const LValue* array, uint64 begin, uint64 end)
	{
		const ArrayType* arr = AsArray(array->type);
		if (begin >= end || !LifecycleFacts::NeedsConstruct(m_Ctx.queries(), arr->GetElementType())) return;
		ForEachIndex(region, begin, end, false, [&](BlockNode* body, const HIRNode* i) { Construct(body, ElementOf(array, i)); });
	}

	void Lifecycle::CopyElement(BlockNode* region, const LValue* array, uint64 source, uint64 begin, uint64 end)
	{
		const LValue* from = ElementOf(array, IndexConstant(source));
		ForEachIndex(region, begin, end, false, [&](BlockNode* body, const HIRNode* i) { CopyInto(body, ElementOf(array, i), from); });
	}

	void Lifecycle::DestroyNow(BlockNode* region, const LValue* object)
	{
		if (ReferenceBinding::Of(object->type)) return; // a reference does not own its referent
		if (const ArrayType* arr = AsArray(object->type))
		{
			if (!LifecycleFacts::NeedsDestroy(m_Ctx.queries(), arr->GetElementType())) return;
			ForEachIndex(region, 0, arr->GetElementCount(), true, [&](BlockNode* body, const HIRNode* i) { DestroyNow(body, ElementOf(object, i)); });
			return;
		}
		if (const HIRNode* call = DestroyCall(object))
			region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(call));
	}

	const HIRNode* Lifecycle::DestroyCall(const LValue* object)
	{
		if (ReferenceBinding::Of(object->type)) return nullptr;
		const ObjectType* obj = AsObject(object->type);
		const FunctionRecord* destroy = obj ? Destroy(obj) : nullptr;
		return destroy ? DirectCall(destroy, object, {}) : nullptr;
	}

	void Lifecycle::DestroyAtExit(BlockNode* region, const LValue* object)
	{
		// One registration covers every element of an array, of any rank.
		if (!LifecycleFacts::NeedsDestroy(m_Ctx.queries(), object->type)) return;
		const Type* element = TypeHandle::UnwrapToValue(object->type);
		uint64 count = 1;
		for (const ArrayType* arr = AsArray(element); arr; arr = AsArray(element))
		{
			count *= arr->GetElementCount();
			element = TypeHandle::UnwrapToValue(arr->GetElementType());
		}
		if (count == 0) return;
		const FunctionRecord* destroy = Destroy(AsObject(element));
		region->stmts.push_back(m_Ctx.out->NewNode<AtExitNode>(object, destroy, count, element->GetSize(), HasThreadStorage(object)));
	}

	bool Lifecycle::HasThreadStorage(const LValue* object) const
	{
		if (object->kind == LValue::Kind::Global) return object->sym && object->sym->isThreadLocal;
		return object->kind == LValue::Kind::Slot && m_Ctx.out->GetSlot(object->slot).isThreadLocal;
	}

	DeferNode* Lifecycle::ScheduleDestroy(BlockNode* region, const LValue* object)
	{
		if (!LifecycleFacts::NeedsDestroy(m_Ctx.queries(), object->type)) return nullptr;
		auto* cleanup = m_Ctx.out->NewNode<DeferNode>();
		cleanup->body = m_Ctx.out->NewNode<BlockNode>(nullptr);
		DestroyNow(cleanup->body, object);
		region->stmts.push_back(cleanup);
		return cleanup;
	}

	void Lifecycle::CopyInto(BlockNode* region, const LValue* target, const LValue* source)
	{
		if (!LifecycleFacts::NeedsCopy(m_Ctx.queries(), target->type))
		{
			region->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(target, m_Ctx.out->NewNode<LoadNode>(source->type, source)));
			return;
		}
		if (const ArrayType* arr = AsArray(target->type))
		{
			ForEachIndex(region, 0, arr->GetElementCount(), false, [&](BlockNode* body, const HIRNode* i) { CopyInto(body, ElementOf(target, i), ElementOf(source, i)); });
			return;
		}
		const ObjectType* obj = AsObject(target->type);
		const FunctionRecord* copy = Copy(obj);
		const Type* ptr = m_Ctx.queries().TypeStore()->Pointer(obj);
		auto* from = m_Ctx.out->NewNode<AddressOfNode>(ptr, source);
		from->transient = true; // the copy's `const T&`
		region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(DirectCall(copy, target, { from })));
	}

	// --- values ------------------------------------------------------------------------------------------

	QueryResult<const HIRNode*> Lifecycle::CopyOf(const LValue* source)
	{
		if (auto copyable = LifecycleFacts::CheckCopyable(m_Ctx.queries(), source->type); !copyable.Ok())
			return QueryResult<const HIRNode*>::Fail(copyable.error, copyable.message);
		if (!LifecycleFacts::NeedsCopy(m_Ctx.queries(), source->type))
			return QueryResult<const HIRNode*>(m_Ctx.out->NewNode<LoadNode>(source->type, source));
		// { temp = zero; copy(temp, source); } then temp's value: the copy is relocated into whatever takes it.
		auto copy = BuildTemporary(source->type, "__copy", [&](BlockNode* steps, const LValue* temp) {
			CopyInto(FillGuard(steps, temp), temp, source);
			return QueryResult<void>::Success();
		});
		if (!copy.Ok() || !MayMove(source)) return copy;
		// A local's copy moves instead when it is the local's last use (LastUseMoves decides after the body).
		auto* transfer = m_Ctx.out->NewNode<TransferNode>(source->type, source, copy.value);
		m_Ctx.transfers.push_back({ transfer, source->slot });
		return QueryResult<const HIRNode*>(transfer);
	}

	const HIRNode* Lifecycle::DefaultValue(const Type* type)
	{
		return BuildTemporary(type, "__constructed", [&](BlockNode* steps, const LValue* temp) {
			Construct(FillGuard(steps, temp), temp);
			return QueryResult<void>::Success();
		}).value;
	}

	QueryResult<const HIRNode*> Lifecycle::BuildTemporary(const Type* type, const char* name,
		const std::function<QueryResult<void>(BlockNode*, const LValue*)>& build)
	{
		// The steps run as a block (an array's are a loop).
		LValue tempLv; tempLv.kind = LValue::Kind::Slot; tempLv.type = type; tempLv.slot = m_Ctx.AllocSlot(type, name, SlotClass::Temp);
		const LValue* temp = m_Ctx.NewLValue(tempLv);
		BlockNode* steps = m_Ctx.out->NewNode<BlockNode>(nullptr);
		steps->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(temp, m_Ctx.out->NewNode<ConstantNode>(type, ConstantValue())));
		if (auto built = build(steps, temp); !built.Ok()) return QueryResult<const HIRNode*>::Fail(built.error, built.message);
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(type);
		seq->steps.push_back(steps);
		seq->steps.push_back(m_Ctx.out->NewNode<LoadNode>(type, temp));
		seq->fresh = true;
		return QueryResult<const HIRNode*>(seq);
	}

	const HIRNode* Lifecycle::Take(const LValue* source)
	{
		const Type* t = source->type;
		uint32 slot = m_Ctx.AllocSlot(t, "__taken", SlotClass::Temp);
		LValue tempLv; tempLv.kind = LValue::Kind::Slot; tempLv.type = t; tempLv.slot = slot;
		const LValue* temp = m_Ctx.NewLValue(tempLv);
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(t);
		seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(temp, m_Ctx.out->NewNode<LoadNode>(t, source)));
		seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(source, m_Ctx.out->NewNode<ConstantNode>(t, ConstantValue())));
		seq->steps.push_back(m_Ctx.out->NewNode<LoadNode>(t, temp));
		seq->fresh = true;
		return seq;
	}

	const HIRNode* Lifecycle::EndFullExpression(const HIRNode* value, size_t mark)
	{
		if (m_Ctx.pendingTemporaries.size() <= mark) return value;
		// The full expression is a block of its own that owns the temporaries it made: they are destroyed as it ends,
		// on every path, and its statement no longer holds them.
		BlockNode* full = m_Ctx.out->NewNode<BlockNode>(nullptr);
		OwnTemporaries(full, mark);
		const Type* t = value->type;
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(t);
		seq->steps.push_back(full);
		if (!t || t->GetKind() == TypeKind::Void)
		{
			full->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(value));
			return seq;
		}
		LValue resultLv; resultLv.kind = LValue::Kind::Slot; resultLv.type = t; resultLv.slot = m_Ctx.AllocSlot(t, "__full", SlotClass::Temp);
		const LValue* result = m_Ctx.NewLValue(resultLv);
		full->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(result, value));
		seq->steps.push_back(m_Ctx.out->NewNode<LoadNode>(t, result));
		return seq;
	}

	void Lifecycle::OwnTemporaries(BlockNode* block, size_t mark)
	{
		for (size_t i = mark; i < m_Ctx.pendingTemporaries.size(); ++i)
		{
			const auto& temp = m_Ctx.pendingTemporaries[i];
			block->stmts.push_back(m_Ctx.out->NewNode<LocalDeclNode>(temp.slot, temp.type, nullptr));
			LValue lv; lv.kind = LValue::Kind::Slot; lv.type = temp.type; lv.slot = temp.slot;
			ScheduleDestroy(block, m_Ctx.NewLValue(lv));
		}
		m_Ctx.pendingTemporaries.erase(m_Ctx.pendingTemporaries.begin() + mark, m_Ctx.pendingTemporaries.end());
	}

	void Lifecycle::PlaceTemporaries(BlockNode* block, size_t mark)
	{
		// The statement's nodes move into a block of their own that owns its temporaries. Cleanups the statement
		// registers for the enclosing scope (a declared variable's destruction, a `defer`) stay in the enclosing
		// block, after it.
		BlockNode* statement = m_Ctx.out->NewNode<BlockNode>(nullptr);
		OwnTemporaries(statement, 0);
		std::vector<HIRNode*> enclosingCleanups;
		for (size_t i = mark; i < block->stmts.size(); ++i)
		{
			HIRNode* node = block->stmts[i];
			if (node->kind == HIRNode::Kind::Defer) enclosingCleanups.push_back(node);
			else statement->stmts.push_back(node);
		}
		block->stmts.erase(block->stmts.begin() + mark, block->stmts.end());
		block->stmts.push_back(statement);
		block->stmts.insert(block->stmts.end(), enclosingCleanups.begin(), enclosingCleanups.end());
	}

	QueryResult<void> Lifecycle::LowerStatement(BlockNode* block, const std::function<QueryResult<void>()>& lower)
	{
		std::vector<LoweringContext::PendingTemporary> outer;
		outer.swap(m_Ctx.pendingTemporaries);
		const size_t mark = block->stmts.size();
		QueryResult<void> r = lower();
		if (r.Ok() && !m_Ctx.pendingTemporaries.empty()) PlaceTemporaries(block, mark);
		m_Ctx.pendingTemporaries.swap(outer);
		return r;
	}

	void Lifecycle::EndTemporaries(BlockNode* region, size_t mark)
	{
		std::vector<const HIRNode*> steps;
		DestroyTemporaries(steps, mark);
		for (const HIRNode* step : steps) region->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(step));
	}

	void Lifecycle::DestroyTemporaries(std::vector<const HIRNode*>& steps, size_t mark)
	{
		// Last created, first destroyed.
		for (size_t i = m_Ctx.pendingTemporaries.size(); i-- > mark;)
		{
			const auto& temp = m_Ctx.pendingTemporaries[i];
			LValue lv; lv.kind = LValue::Kind::Slot; lv.type = temp.type; lv.slot = temp.slot;
			const LValue* object = m_Ctx.NewLValue(lv);
			if (const HIRNode* call = DestroyCall(object)) steps.push_back(call);
			steps.push_back(m_Ctx.out->NewNode<StoreNode>(object, m_Ctx.out->NewNode<ConstantNode>(temp.type, ConstantValue())));
		}
	}

	bool Lifecycle::AssignmentNeedsLifecycle(const Type* t)
	{
		const Type* v = TypeHandle::UnwrapToValue(t);
		if (const ObjectType* obj = AsObject(v); obj && LifecycleFacts::Own(m_Ctx.queries(), obj).assign) return true;
		return LifecycleFacts::NeedsDestroy(m_Ctx.queries(), v) || LifecycleFacts::NeedsCopy(m_Ctx.queries(), v);
	}

	QueryResult<const LValue*> Lifecycle::Assign(const LValue* named, const Value& source)
	{
		const Type* t = TypeHandle::UnwrapToValue(named->type);
		const ObjectType* obj = AsObject(t);
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(m_Ctx.queries().TypeStore()->Pointer(t));
		// The target is named for the destroy, the store and the result: what it is made of runs once (BUG-305).
		const LValue* target = EvaluatedOnce(m_Ctx, named, seq->steps);
		const FunctionRecord* assign = obj ? LifecycleFacts::Own(m_Ctx.queries(), obj).assign : nullptr;
		if (assign)
		{
			auto arg = BindArgument(m_Ctx, source, m_Ctx.LoadIfNeeded(source), assign->type->GetParameters()[0].m_Type);
			if (!arg.Ok()) return QueryResult<const LValue*>::Fail(arg.error, arg.message);
			seq->steps.push_back(DirectCall(assign, target, { arg.value }));
		}
		else
		{
			if (!obj)
				return QueryResult<const LValue*>::Fail(QueryError::Unsupported, "assigning an array of '" +
					static_cast<const ArrayType*>(t)->GetElementType()->GetName() + "' needs element-wise destruction; assign the elements");
			auto value = InitialValue(m_Ctx, source, t);
			if (!value.Ok()) return QueryResult<const LValue*>::Fail(value.error, value.message);
			uint32 slot = m_Ctx.AllocSlot(t, "__assigned", SlotClass::Temp);
			LValue tempLv; tempLv.kind = LValue::Kind::Slot; tempLv.type = t; tempLv.slot = slot;
			const LValue* temp = m_Ctx.NewLValue(tempLv);
			seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(temp, value.value));
			if (const FunctionRecord* destroy = Destroy(obj)) seq->steps.push_back(DirectCall(destroy, target, {}));
			seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(target, m_Ctx.out->NewNode<LoadNode>(t, temp)));
		}
		// The result names the target again; a use of it that keeps the address is seen through this Deref.
		auto* address = m_Ctx.out->NewNode<AddressOfNode>(seq->type, target);
		address->transient = true;
		seq->steps.push_back(address);
		LValue result; result.kind = LValue::Kind::Deref; result.type = t; result.addr = seq;
		return QueryResult<const LValue*>(m_Ctx.NewLValue(result));
	}

	bool Lifecycle::IsFresh(const HIRNode* node)
	{
		if (!node) return false;
		switch (node->kind)
		{
		case HIRNode::Kind::Construct: return true;
		case HIRNode::Kind::Call:
		{
			const Type* t = node->type;
			return t && t->GetKind() != TypeKind::Reference && (AsObject(t) || AsArray(t));
		}
		case HIRNode::Kind::Sequence: return static_cast<const SequenceNode*>(node)->fresh;
		case HIRNode::Kind::Transfer: return true;
		case HIRNode::Kind::Ternary: return node->type->GetKind() != TypeKind::Reference && AsObject(node->type) != nullptr;
		default: return false;
		}
	}

	bool Lifecycle::MayMove(const LValue* source) const
	{
		if (source->kind != LValue::Kind::Slot) return false;
		const SlotClass cls = m_Ctx.out->GetSlot(source->slot).cls;
		return cls == SlotClass::Local || cls == SlotClass::Param;
	}

	const LValue* Lifecycle::Temporary(const HIRNode* fresh)
	{
		const Type* t = fresh->type;
		uint32 slot = m_Ctx.AllocSlot(t, "__temporary", SlotClass::Temp);
		m_Ctx.pendingTemporaries.push_back({ slot, t });
		LValue slotLv; slotLv.kind = LValue::Kind::Slot; slotLv.type = t; slotLv.slot = slot;
		const LValue* temp = m_Ctx.NewLValue(slotLv);
		auto* seq = m_Ctx.out->NewNode<SequenceNode>(m_Ctx.queries().TypeStore()->Pointer(t));
		seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(temp, fresh));
		seq->steps.push_back(m_Ctx.out->NewNode<AddressOfNode>(seq->type, temp));
		LValue deref; deref.kind = LValue::Kind::Deref; deref.type = t; deref.addr = seq;
		return m_Ctx.NewLValue(deref);
	}

	const HIRNode* Lifecycle::Discarded(const Value& v)
	{
		const Type* t = TypeHandle::UnwrapToValue(v.type);
		bool aggregate = t && (t->GetKind() == TypeKind::Object || t->GetKind() == TypeKind::Array);
		const Type* pointer = aggregate ? m_Ctx.queries().TypeStore()->Pointer(t) : nullptr;
		if (v.isLValue && aggregate)
		{
			auto* address = m_Ctx.out->NewNode<AddressOfNode>(pointer, v.lvalue);
			address->transient = true; // evaluated for its effects only
			return address;
		}
		if (!v.isLValue && aggregate && IsFresh(v.rvalue) && LifecycleFacts::NeedsDestroy(m_Ctx.queries(), t))
			return m_Ctx.out->NewNode<AddressOfNode>(pointer, Temporary(v.rvalue));
		return m_Ctx.LoadIfNeeded(v);
	}

	const LValue* Lifecycle::AddressableObject(const Value& value, const Type* type)
	{
		if (value.isLValue) return value.lvalue;
		const HIRNode* node = value.rvalue;
		if (!node) return nullptr;
		LValue lv;
		if (AsObject(type) && node->kind == HIRNode::Kind::Call && !(IsFresh(node) && LifecycleFacts::NeedsDestroy(m_Ctx.queries(), type)))
		{
			const_cast<CallNode*>(static_cast<const CallNode*>(node))->materialize = true; // HIR nodes live in the arena
			lv.kind = LValue::Kind::Deref; lv.type = type; lv.addr = node;
		}
		else lv = *Temporary(node); // a statement temporary, destroyed when the statement ends (if it needs to be)
		lv.ephemeral = true;
		return m_Ctx.NewLValue(lv);
	}

	const HIRNode* Lifecycle::Hold(const HIRNode* value, std::vector<const HIRNode*>& prelude)
	{
		const Type* t = value->type;
		const bool owned = LifecycleFacts::NeedsDestroy(m_Ctx.queries(), t);
		LValue lv; lv.kind = LValue::Kind::Slot; lv.type = t; lv.slot = m_Ctx.AllocSlot(t, "__held", SlotClass::Temp);
		if (owned) m_Ctx.pendingTemporaries.push_back({ lv.slot, t });
		const LValue* slot = m_Ctx.NewLValue(lv);
		prelude.push_back(m_Ctx.out->NewNode<StoreNode>(slot, value));
		if (owned) return Take(slot);
		return m_Ctx.out->NewNode<LoadNode>(t, slot);
	}

	void Lifecycle::CopyBase(BlockNode* root, const ObjectType* type, const LValue* other)
	{
		const ObjectType* base = LifecycleFacts::ClassBase(type);
		if (!base) return;
		CopyInto(root, BaseOf(This(type), type, base), BaseOf(other, type, base));
	}

	void Lifecycle::CopyUnionRegions(BlockNode* root, const ObjectType* type, const LValue* other)
	{
		LifecycleFacts::ForEachUnionRegion(type, [&](const ObjectType::FieldInfo& first) {
			const LValue* source = UnionRegionOf(other, first);
			root->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(UnionRegionOf(This(type), first), m_Ctx.out->NewNode<LoadNode>(source->type, source)));
		});
	}

	// --- helpers -----------------------------------------------------------------------------------------

	const LValue* Lifecycle::This(const ObjectType* type)
	{
		LValue lv; lv.kind = LValue::Kind::This; lv.type = type;
		return m_Ctx.NewLValue(lv);
	}

	const LValue* Lifecycle::FieldOf(const LValue* object, const ObjectType::FieldInfo& field)
	{
		LValue lv; lv.kind = LValue::Kind::Field; lv.type = field.m_Type; lv.base = object; lv.field = &field;
		return m_Ctx.NewLValue(lv);
	}

	const LValue* Lifecycle::BaseOf(const LValue* object, const ObjectType* type, const ObjectType* base)
	{
		return SubobjectLValue(m_Ctx, object, base, ClassHierarchy::LocateBase(type, base).offset);
	}

	const LValue* Lifecycle::UnionRegionOf(const LValue* object, const ObjectType::FieldInfo& first)
	{
		auto* types = m_Ctx.queries().TypeStore();
		LValue lv; lv.kind = LValue::Kind::Base; lv.base = object;
		lv.type = types->Array(types->Builtin(TypeKind::UInt8), first.m_UnionRegionSize);
		lv.baseOffset = static_cast<int64>(first.m_Offset - first.m_UnionIntraOffset);
		lv.ephemeral = object->ephemeral;
		return m_Ctx.NewLValue(lv);
	}

	const HIRNode* Lifecycle::DirectCall(const FunctionRecord* rec, const LValue* receiver, std::vector<const HIRNode*> args)
	{
		auto* call = m_Ctx.out->NewNode<CallNode>(rec->type ? rec->type->GetReturnType() : nullptr, rec);
		BindReceiver(m_Ctx, call, receiver, CallDispatch::Direct);
		for (const HIRNode* a : args) call->args.push_back(a);
		return call;
	}

	const LValue* Lifecycle::ElementOf(const LValue* array, const HIRNode* index)
	{
		const ArrayType* arr = AsArray(array->type);
		return BuildIndexLValue(m_Ctx, Value::L(array), index, arr->GetElementType(), arr->GetElementType()->GetSize());
	}

	const HIRNode* Lifecycle::IndexConstant(uint64 index)
	{
		return m_Ctx.out->NewNode<ConstantNode>(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64), ConstantValue::Int(static_cast<int64>(index)));
	}

	void Lifecycle::ForEachIndex(BlockNode* region, uint64 begin, uint64 end, bool reverse,
		const std::function<void(BlockNode*, const HIRNode*)>& visit)
	{
		// One index needs no loop.
		if (begin >= end) return;
		if (end - begin == 1)
		{
			visit(region, IndexConstant(begin));
			return;
		}
		IndexRange range{ [&] { return IndexConstant(begin); }, [&] { return IndexConstant(end); } };
		AppendIndexLoop(m_Ctx, region, range, reverse, visit);
	}
} // namespace Scripting
