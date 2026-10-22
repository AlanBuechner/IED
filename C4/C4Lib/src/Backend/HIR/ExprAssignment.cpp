#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/EvaluatedOnce.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Model/Types/ClassHierarchy.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/PointerConversion.h"
#include "Parser/Nodes/Expressions/AssignmentExpressionNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"
#include "Model/Types.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/SignatureType.h"
#include "Model/TypeHandle.h"
#include <string>

namespace Scripting
{
	namespace
	{
		// Binding a reference field in its create writes the field, not its referent, so a `const T&` binds too.
		bool BindsReference(const LoweringContext& ctx, const Value& target)
		{
			return target.isLValue && ReferenceBinding::BindsInCreate(ctx, target.lvalue);
		}
	}

	// Assignments: `target = value` and the compound forms. The target is resolved, checked writable, and
	// given the value - by its lifecycle when it has one (D-06), else by a store.

	QueryResult<Value> ExprLowerer::ResolveAssignment(const AssignmentExpressionNode* n)
	{
		const Core::String tok = n->GetOperatorToken().GetText();
		auto target = ResolveExpr(n->GetLeftOperand().Get());
		if (!target.Ok()) return target;
		if (tok != "=" || !BindsReference(m_Ctx, target.value))
			if (auto writable = CheckWritable(n, target.value); !writable.Ok()) return QueryResult<Value>::Fail(writable.error, writable.message);
		if (const auto* brace = dynamic_cast<const BracedInitializerNode*>(n->GetRightOperand().Get()); brace && LanesTakeBraces(target.value, *brace))
			return AssignLanes(n, target.value, *brace);
		auto value = tok == "=" ? ResolveFor(n->GetRightOperand().Get(), target.value.type) : ResolveExpr(n->GetRightOperand().Get());
		if (!value.Ok()) return value;
		if (tok == "=") ImplicitConversion::WarnIfNarrowing(m_Ctx, n->GetRightOperand().Get(), value.value, target.value.type);
		return tok == "=" ? AssignValue(n, target.value, value.value) : CompoundAssign(n, tok, target.value, value.value);
	}

	QueryResult<Value> ExprLowerer::AssignTo(const AstNode* site, const AstNode* targetNode, const Value& value)
	{
		auto target = ResolveExpr(targetNode);
		if (!target.Ok()) return target;
		if (!BindsReference(m_Ctx, target.value))
			if (auto writable = CheckWritable(site, target.value); !writable.Ok()) return QueryResult<Value>::Fail(writable.error, writable.message);
		return AssignValue(site, target.value, value);
	}

	QueryResult<void> ExprLowerer::CheckWritable(const AstNode* n, const Value& target)
	{
		// A store through a by-value call temp (GetV().x = 5) or a const chain (p->inner.n = 5 where p is
		// const Outer*) is ill-formed: the former would discard into a dead temporary, the latter mutates
		// read-only data.
		if (!target.isLValue) return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "cannot assign to an rvalue"));
		Core::String why;
		if (ChainBlocksWrite(target.lvalue, why))
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "cannot write through this location: " + why));
		// D-37: a run that names one lane twice (`v.xx = ...`) would write that lane twice.
		if (target.lvalue->kind == LValue::Kind::Swizzle)
		{
			const auto& lanes = target.lvalue->laneIndices;
			for (size_t i = 0; i < lanes.size(); ++i)
				for (size_t j = 0; j < i; ++j)
					if (lanes[i] == lanes[j])
						return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue,
							"this swizzle names a lane twice, so it cannot be written to: name each lane once (it can still be read, as a splat)"));
		}
		return QueryResult<void>::Success();
	}

	bool ExprLowerer::LanesTakeBraces(const Value& target, const BracedInitializerNode& brace) const
	{
		// `v.xz = { 1.0f, 2.0f }`: one value per lane of a swizzle run, `v.x = { 5.0f }`, or every lane of a
		// `packed T[N]` value.
		const auto* vector = dynamic_cast<const SwizzleType*>(TypeHandle::UnwrapToValue(target.type));
		const bool swizzle = target.lvalue->kind == LValue::Kind::Swizzle;
		const bool lane = target.lvalue->kind == LValue::Kind::Lane;
		if (!vector && !lane) return false;
		const Type* element = vector ? vector->GetElementType() : target.type;
		const uint32 lanes = swizzle ? static_cast<uint32>(target.lvalue->laneIndices.size()) : vector ? vector->GetLaneCount() : 1;
		return element && brace.ElementCount() == lanes;
	}

	QueryResult<Value> ExprLowerer::AssignLanes(const AstNode* n, const Value& target, const BracedInitializerNode& brace)
	{
		// The braced lane values, each converted to the element type: a scalar lane stores one; an unpacked run
		// (a struct of fields) stores each field; a packed run or value builds the vector and stores it whole.
		const bool swizzle = dynamic_cast<const SwizzleType*>(TypeHandle::UnwrapToValue(target.type)) != nullptr;
		const Type* elemT = swizzle ? static_cast<const SwizzleType*>(TypeHandle::UnwrapToValue(target.type))->GetElementType() : target.type;
		if (!swizzle)
		{
			auto ev = ResolveExpr(brace.GetElements()[0].Get());
			if (!ev.Ok()) return ev;
			const HIRNode* v = m_Ctx.CoerceValue(m_Ctx.LoadIfNeeded(ev.value), elemT);
			return Value::R(m_Ctx.out->NewNode<StoreNode>(target.lvalue, v), v->type);
		}
		const auto& idxs = target.lvalue->laneIndices;
		const Type* baseType = target.lvalue->base ? target.lvalue->base->type : nullptr;
		const auto* uobj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(baseType));
		if (target.lvalue->isUnpacked && uobj)
		{
			const size_t esz = elemT->GetSize();
			auto seq = m_Ctx.out->NewNode<SequenceNode>(elemT);
			for (uint32 i = 0; i < idxs.size(); ++i)
			{
				// A lane's index maps to its field by byte offset (index * element size) - the names are r/g/b/a,
				// not a/b/c/d, so reconstructing a name from the index is wrong.
				const size_t wantOff = static_cast<size_t>(idxs[i]) * esz;
				const ObjectType::FieldInfo* fld = nullptr;
				for (const auto& ff : uobj->GetFields()) if (!ff.m_IsStatic && ff.m_Offset == wantOff) { fld = &ff; break; }
				if (!fld) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, "no field for lane index " + std::to_string(idxs[i])));
				auto ev = ResolveExpr(brace.GetElements()[i].Get());
				if (!ev.Ok()) return ev;
				const HIRNode* v = m_Ctx.CoerceValue(m_Ctx.LoadIfNeeded(ev.value), fld->m_Type);
				LValue lv; lv.kind = LValue::Kind::Field; lv.type = fld->m_Type; lv.field = fld; lv.base = target.lvalue->base;
				seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(lv), v));
			}
			return Value::R(seq, elemT);
		}
		auto vec = m_Ctx.out->NewNode<VectorBuildNode>(target.type);
		for (uint32 i = 0; i < brace.ElementCount(); ++i)
		{
			auto ev = ResolveExpr(brace.GetElements()[i].Get());
			if (!ev.Ok()) return ev;
			vec->elems.push_back(m_Ctx.CoerceValue(m_Ctx.LoadIfNeeded(ev.value), elemT));
		}
		return Value::R(m_Ctx.out->NewNode<StoreNode>(target.lvalue, vec), target.type);
	}

	QueryResult<Value> ExprLowerer::AssignValue(const AstNode* n, const Value& target, const Value& value)
	{
		// In its object's create, `r = x` binds the reference field `r` (where C++ writes a member initializer).
		if (ReferenceBinding::BindsInCreate(m_Ctx, target.lvalue))
		{
			const LValue* field = target.lvalue->boundBy;
			const ReferenceType* ref = ReferenceBinding::Of(field->type);
			auto address = m_Ctx.Fail(n, ReferenceBinding::Address(m_Ctx, ref, value, ReferenceBinding::PointerFor(m_Ctx, ref), field->field->mName));
			if (!address.Ok()) return QueryResult<Value>::Fail(address.error, address.message);
			m_Ctx.boundReferences.push_back(field->field);
			return Value::R(ReferenceBinding::Bind(m_Ctx, field, address.value), address.value->type);
		}
		// An object with a lifecycle is assigned by D-06 (its `=`, or new value / destroy old / relocate).
		if (target.lvalue->kind != LValue::Kind::IndexOp && Lifecycle(m_Ctx).AssignmentNeedsLifecycle(target.type))
		{
			Value source = value;
			// A `const T&` parameter read as its address (a pointer to T) assigns the object behind it.
			const Type* targetT = TypeHandle::UnwrapToValue(target.type);
			const Type* sourceT = TypeHandle::UnwrapToValue(source.type);
			if (sourceT && sourceT->GetKind() == TypeKind::Pointer && TypeHandle::UnwrapToValue(static_cast<const PointerType*>(sourceT)->GetPointeeType()) == targetT)
			{
				LValue deref; deref.kind = LValue::Kind::Deref; deref.type = targetT; deref.addr = m_Ctx.LoadIfNeeded(source);
				source = Value::L(m_Ctx.NewLValue(deref));
			}
			auto part = AssignedPart(n, targetT, source);
			if (!part.Ok()) return QueryResult<Value>::Fail(part.error, part.message);
			auto assigned = Lifecycle(m_Ctx).Assign(target.lvalue, part.value);
			if (!assigned.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(assigned.error, assigned.message));
			return Value::L(assigned.value);
		}
		auto stored = AssignedValue(n, target, value);
		if (!stored.Ok()) return QueryResult<Value>::Fail(stored.error, stored.message);
		return StoreAssigned(n, "=", target, stored.value);
	}

	QueryResult<const HIRNode*> ExprLowerer::AssignedValue(const AstNode* n, const Value& target, const Value& value)
	{
		using Result = QueryResult<const HIRNode*>;
		const HIRNode* loaded = m_Ctx.LoadIfNeeded(value);
		// `ptr = this;`: C++'s `this` IS a T*, but C4 models it as the object LOCATION. When the target is a
		// pointer, the value is the address of the This location - the this-pointer.
		if (value.isLValue && IsThisObject(value.lvalue))
		{
			const Type* tgtVal = target.type ? TypeHandle::UnwrapToValue(target.type) : nullptr;
			if (tgtVal && tgtVal->GetKind() == TypeKind::Pointer)
				loaded = m_Ctx.out->NewNode<AddressOfNode>(target.type, value.lvalue);
		}
		// Pointer compatibility: CoerceValue would convert ANY pointer to any pointer (a downcast or an unrelated
		// A*/B* would silently compile), so the conversion is checked first.
		if (auto converts = CheckPointerConversion(target.type, loaded); !converts.Ok())
			return m_Ctx.Fail(n, Result::Fail(converts.error, converts.error == QueryError::Ambiguous ? converts.message
				: "Cannot assign value of type '" + Core::String(loaded->type->GetName()) + "' to '" +
					TypeHandle::UnwrapToValue(target.type)->GetName() + "': " + converts.message));
		// Whole-object copy from a reference/pointer-to-the-object: `data[i] = v` where v is a `const T&` param
		// (registered as T*) or an OA* rvalue. The loaded value is the referent's ADDRESS, so it is loaded through.
		const Type* targetObj = target.type ? TypeHandle::UnwrapToValue(target.type) : nullptr;
		const bool isObjectTarget = targetObj && targetObj->GetKind() == TypeKind::Object;
		const Type* valType = loaded ? loaded->type : nullptr;
		const Type* pointee = nullptr;
		if (isObjectTarget && valType)
		{
			if (valType->GetKind() == TypeKind::Pointer) pointee = static_cast<const PointerType*>(valType)->GetPointeeType();
			else if (valType->GetKind() == TypeKind::Reference) pointee = static_cast<const ReferenceType*>(valType)->GetReferentType();
		}
		if (isObjectTarget && pointee && pointee == targetObj)
		{
			LValue derefLv; derefLv.kind = LValue::Kind::Deref; derefLv.type = targetObj; derefLv.addr = loaded;
			return Result(m_Ctx.CoerceValue(m_Ctx.LoadIfNeeded(Value::L(m_Ctx.NewLValue(derefLv))), target.type));
		}
		if (const auto* signature = dynamic_cast<const SignatureType*>(TypeHandle::UnwrapToValue(target.type)))
		{
			auto converted = InitialValue(m_Ctx, value, signature); // a lambda / funcptr / null into a func or funcptr
			if (!converted.Ok()) return m_Ctx.Fail(n, Result::Fail(converted.error, converted.message));
			return converted;
		}
		const Type* to = TypeHandle::UnwrapToValue(target.type);
		if (to->GetKind() == TypeKind::Object || to->GetKind() == TypeKind::Array)
		{
			auto part = AssignedPart(n, to, value);
			if (!part.Ok()) return Result::Fail(part.error, part.message);
			return Result(m_Ctx.LoadIfNeeded(part.value));
		}
		auto converted = ImplicitConversion::Scalar(m_Ctx, loaded, to);
		if (!converted.Ok())
			return m_Ctx.Fail(n, Result::Fail(converted.error, "Cannot assign value of type '" + Core::String(TypeHandle::UnwrapToValue(loaded->type)->GetName()) +
				"' to '" + to->GetName() + "': " + converted.message));
		return converted;
	}

	QueryResult<Value> ExprLowerer::AssignedPart(const AstNode* n, const Type* target, const Value& value)
	{
		// The value an object variable takes: the object itself, or the base sub-object of a derived one (slicing).
		const Type* from = TypeHandle::UnwrapToValue(value.type);
		if (Core::String why = ImplicitConversion::ObjectProblem(from, target); !why.empty())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "Cannot assign value of type '" + Core::String(from->GetName()) +
				"' to '" + target->GetName() + "': " + why));
		const auto* derived = dynamic_cast<const ObjectType*>(from);
		const auto* base = dynamic_cast<const ObjectType*>(target);
		if (from == target || !derived || !base) return QueryResult<Value>(value);
		const LValue* whole = Lifecycle(m_Ctx).AddressableObject(value, derived);
		return Value::L(SubobjectLValue(m_Ctx, whole, base, ClassHierarchy::LocateBase(derived, base).offset));
	}

	QueryResult<Value> ExprLowerer::CompoundAssign(const AstNode* n, const Core::String& tok, const Value& written, const Value& value)
	{
		// The target is read and written: what it is computed from runs once (BUG-201).
		std::vector<const HIRNode*> steps;
		const Value target = Value::L(EvaluatedOnce(m_Ctx, written.lvalue, steps));
		auto assigned = CompoundAssignOnce(n, tok, target, value);
		if (!assigned.Ok()) return assigned;
		const HIRNode* result = AfterSteps(m_Ctx, steps, m_Ctx.LoadIfNeeded(assigned.value));
		return Value::R(result, result->type);
	}

	QueryResult<Value> ExprLowerer::CompoundAssignOnce(const AstNode* n, const Core::String& tok, const Value& target, const Value& value)
	{
		// `target OP= value`: an in-place operator method ([operator("add_assign")]) writes the target itself;
		// the plain operator (a method, or the built-in one) computes the value that is stored back.
		const Type* tt = TypeHandle::UnwrapToValue(target.type);
		const Type* rt = TypeHandle::UnwrapToValue(value.type);
		auto op = m_Ctx.queries().QueryOperator(tok, tt, rt, m_Ctx.unitId, m_Ctx.enclosingType, AccessOf(target), AccessOf(value));
		if (op.error == QueryError::ReadOnly) return m_Ctx.Fail(n, QueryResult<Value>::Fail(op.error, op.message));
		if (op.Ok() && op.value.record)
		{
			auto call = EmitOperatorCall(n, op.value, target, value);
			if (!call.Ok()) return call;
			if (!op.value.isAssignForm) return StoreAssigned(n, tok, target, m_Ctx.LoadIfNeeded(call.value));
			// The in-place form already wrote the target: keep the call and yield the updated value, so
			// `int32 x = p += q;` sees the result.
			auto seq = m_Ctx.out->NewNode<SequenceNode>(target.type);
			seq->steps.push_back(call.value.isLValue ? call.value.lvalue->addr : call.value.rvalue);
			seq->steps.push_back(m_Ctx.LoadIfNeeded(target));
			return Value::R(seq, target.type);
		}
		// The built-in operator: exactly `target = target OP value` (pointer offsets, conversions and operand
		// rules included), converted back to the target's type.
		auto combined = ApplyBinary(n, tok.substr(0, tok.size() - 1), target, value); // "a += b" -> "+"
		if (!combined.Ok()) return combined;
		// A pointer stays a pointer: `p -= q` makes a distance, `n += p` an address.
		const Type* made = TypeHandle::UnwrapToValue(combined.value.type);
		if ((tt->GetKind() == TypeKind::Pointer) != (made && made->GetKind() == TypeKind::Pointer))
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "'" + tok + "' cannot store its result, a '" +
				Core::String(made ? made->GetName() : "?") + "', back into the '" + tt->GetName() + "' it changes"));
		return StoreAssigned(n, tok, target, m_Ctx.CoerceValue(m_Ctx.LoadIfNeeded(combined.value), tt));
	}

	QueryResult<Value> ExprLowerer::StoreAssigned(const AstNode* n, const Core::String& tok, const Value& target, const HIRNode* toStore)
	{
		// The assignment IS a store, returned as the result so the statement attaches it.
		const LValue* tv = target.lvalue;
		if (tv->kind != LValue::Kind::IndexOp) return Value::R(m_Ctx.out->NewNode<StoreNode>(tv, toStore), toStore->type);
		// A by-value index has no address: `obj[i] OP= v` dispatches to [operator("[]=")](i, v); without a setter
		// there is nowhere to write.
		if (!tv->setter)
		{
			const Core::String msg = tok == "=" ? "Add a setter, or make the indexer return a reference." : "the new value has nowhere to be stored";
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, "index assignment on a by-value-only indexer: " + msg));
		}
		if (auto allowed = AccessCheck::CheckCall(m_Ctx, *tv->setter); !allowed.Ok())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));
		// C semantics: the expression yields the stored value and the right side is evaluated once - kept in a
		// temporary handed to the setter, then yielded.
		uint32 tmp = m_Ctx.AllocSlot(tv->type, "__index_value", SlotClass::Local);
		LValue tmpLv; tmpLv.kind = LValue::Kind::Slot; tmpLv.type = tv->type; tmpLv.slot = tmp;
		const HIRNode* loadTmp = m_Ctx.out->NewNode<LoadNode>(tv->type, m_Ctx.NewLValue(tmpLv));
		auto scall = m_Ctx.out->NewNode<CallNode>(tv->setter->type->GetReturnType(), tv->setter);
		scall->isOperatorCall = true;
		scall->receiver = m_Ctx.NewLValue(*tv->receiver);
		const auto& sp = tv->setter->type->GetParameters();
		for (uint32 i = 0; i < sp.Count(); ++i)
		{
			// Parameter 0 is the index; the rest carry the value (the `SetIndex(i, v)` shape).
			const Type* pt = sp[i].m_Type ? TypeHandle::UnwrapToValue(sp[i].m_Type) : nullptr;
			const HIRNode* a = i == 0 ? tv->idxNode : loadTmp;
			if (pt && a && a->type != pt) a = m_Ctx.out->NewNode<ConvertNode>(a, a->type, pt);
			scall->args.push_back(a);
		}
		auto seq = m_Ctx.out->NewNode<SequenceNode>(tv->type);
		seq->steps.push_back(m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(tmpLv), toStore));
		seq->steps.push_back(scall);
		seq->steps.push_back(loadTmp);
		return Value::R(seq, tv->type);
	}
} // namespace Scripting
