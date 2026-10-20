#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/CastRules.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/SimdOperands.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/TypeHandle.h"
#include "Model/Types.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/SwizzleType.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/SafeCastNode.h"
#include "Parser/Nodes/Expressions/TypeCheckNode.h"

// The casts that need more than a conversion: to a reference, and the checked ones (`is`, `as`, dynamic_cast).
namespace Scripting
{
	// `(T&)x`, `static_cast<T&>(x)`: the location `x` names, seen as a `T` - a related object's sub-object, or the
	// same bytes for reinterpret_cast. The result is a location, as in C++; the cast rules apply to the pointers.
	QueryResult<Value> ExprLowerer::ResolveReferenceCast(const CastExpressionNode* n, const Value& operand, const ReferenceType* target)
	{
		if (n->GetKind() == CastExpressionNode::CastKind::DynamicCast) return ResolveCheckedReferenceCast(n, operand, target);
		if (!operand.isLValue)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "a cast to a reference needs a location to refer to, not a temporary"));
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* referent = TypeHandle::UnwrapToValue(target->GetReferentType());
		const Type* fromPointer = AddressType(m_Ctx, operand.lvalue);
		const Type* toPointer = ts->Pointer(referent, target->IsConstReferent());
		if (Core::String why = CastRules::WhyRejected(n->GetKind(), fromPointer, toPointer); !why.empty())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));
		const HIRNode* address = m_Ctx.out->NewNode<AddressOfNode>(fromPointer, operand.lvalue);
		const HIRNode* viewed = fromPointer == toPointer ? address : m_Ctx.out->NewNode<ConvertNode>(address, fromPointer, toPointer);
		LValue lv; lv.kind = LValue::Kind::Deref; lv.type = referent; lv.addr = viewed;
		return Value::L(m_Ctx.NewLValue(lv));
	}

	// `x as T&`, `dynamic_cast<T&>(x)` (D-11): the `T` that `x` is, as a location; when `x` is not one, BadCast (the
	// prelude's) is thrown. An upcast always succeeds and checks nothing.
	QueryResult<Value> ExprLowerer::ResolveCheckedReferenceCast(const AstNode* n, const Value& operand, const ReferenceType* target)
	{
		if (!operand.isLValue)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "a checked cast to a reference needs a location to refer to, not a temporary"));
		const Type* referent = TypeHandle::UnwrapToValue(target->GetReferentType());
		const Type* fromPointer = AddressType(m_Ctx, operand.lvalue);
		const bool readOnly = static_cast<const PointerType*>(fromPointer)->IsConstReferent();
		if (readOnly && !target->IsConstReferent())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
				"the object is const, so a checked cast of it gives a 'const " + referent->GetName() + "&'"));
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* toPointer = ts->Pointer(referent, target->IsConstReferent());
		const HIRNode* address = m_Ctx.out->NewNode<AddressOfNode>(fromPointer, operand.lvalue);
		auto cast = ResolveDynamicCast(n, Value::R(address, fromPointer), toPointer, false);
		if (!cast.Ok()) return cast;
		const HIRNode* pointer = m_Ctx.LoadIfNeeded(cast.value);
		if (dynamic_cast<const DynamicCastNode*>(pointer))
		{
			auto checked = ObjectOrBadCast(n, pointer, referent);
			if (!checked.Ok()) return QueryResult<Value>::Fail(checked.error, checked.message);
			pointer = checked.value;
		}
		LValue lv; lv.kind = LValue::Kind::Deref; lv.type = referent; lv.addr = pointer;
		return Value::L(m_Ctx.NewLValue(lv));
	}

	// `pointer` once, or the prelude's __c4_bad_cast (it throws BadCast naming `referent`) when it is null.
	QueryResult<const HIRNode*> ExprLowerer::ObjectOrBadCast(const AstNode* n, const HIRNode* pointer, const Type* referent)
	{
		using Result = QueryResult<const HIRNode*>;
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* name = ts->Pointer(ts->Builtin(TypeKind::Char8), true);
		const FunctionRecord* badCast = nullptr;
		auto found = m_Ctx.queries().QueryFunction("__c4_bad_cast", nullptr, nullptr, Core::Vector<const Type*>{ name }, m_Ctx.unitId,
			m_Ctx.enclosingType, m_Ctx.scope, &badCast);
		if (!found.Ok() || !badCast)
			return m_Ctx.Fail(n, Result::Fail(QueryError::NotFound, "a checked cast to a reference throws the prelude's BadCast, and this "
				"program has no prelude: the default library (Bedrock) is missing"));
		const uint32 slot = m_Ctx.AllocSlot(pointer->type, "__checked_cast", SlotClass::Local);
		LValue temp; temp.kind = LValue::Kind::Slot; temp.type = pointer->type; temp.slot = slot;
		const LValue* held = m_Ctx.NewLValue(temp);
		auto* failure = m_Ctx.out->NewNode<CallNode>(found.value->GetReturnType(), badCast);
		failure->args.push_back(m_Ctx.out->NewNode<ConstantNode>(name, ConstantValue::String(std::string(QualifiedName(referent).c_str()), 1)));
		const HIRNode* isObject = m_Ctx.out->NewNode<IntrinsicOpNode>(ts->Builtin(TypeKind::Bool), IntrinsicOp::Ne,
			m_Ctx.out->NewNode<LoadNode>(pointer->type, held), m_Ctx.out->NewNode<ConstantNode>(pointer->type, ConstantValue::Int(0)));
		auto* sequence = m_Ctx.out->NewNode<SequenceNode>(pointer->type);
		sequence->steps.push_back(m_Ctx.out->NewNode<StoreNode>(held, pointer));
		sequence->steps.push_back(m_Ctx.out->NewNode<TernaryNode>(pointer->type, isObject, m_Ctx.out->NewNode<LoadNode>(pointer->type, held),
			m_Ctx.out->NewNode<ConvertNode>(failure, failure->type, pointer->type)));
		return Result::Success(sequence);
	}

	// `x is T`: true when the dynamic type of `x` is `T` or derives from / implements it. A pointer to a
	// polymorphic object is checked at run time; anything else is decided by its static type.
	QueryResult<Value> ExprLowerer::ResolveTypeCheck(const TypeCheckNode* n)
	{
		auto target = m_Ctx.Fail(n, m_Ctx.queries().ResolveTypeCtx(*n->GetTargetType().Get(), m_Ctx.unitId, m_Ctx.scope));
		if (!target.Ok()) return QueryResult<Value>::Fail(target.error, target.message);
		auto operand = ResolveExpr(n->GetOperand().Get());
		if (!operand.Ok()) return operand;
		operand.value = ThisAsPointer(operand.value); // `this is T` checks the receiver's dynamic type
		const Type* boolT = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool);
		const Type* from = TypeHandle::UnwrapToValue(operand.value.type);
		const Type* to = TypeHandle::UnwrapToValue(target.value);
		const auto* toObj = dynamic_cast<const ObjectType*>(to->GetKind() == TypeKind::Pointer ? TypeHandle::GetPointedType(to) : to);
		const auto* fromObj = dynamic_cast<const ObjectType*>(from->GetKind() == TypeKind::Pointer ? TypeHandle::GetPointedType(from) : from);
		if (from->GetKind() == TypeKind::Pointer && fromObj && toObj && fromObj->IsPolymorphic())
			return ResolveDynamicCast(n, operand.value, m_Ctx.queries().TypeStore()->Pointer(toObj), true);
		bool statically = (from == to) || (fromObj && toObj && ClassHierarchy::DerivesFrom(fromObj, toObj));
		return Value::R(m_Ctx.out->NewNode<ConstantNode>(boolT, ConstantValue::Bool(statically)), boolT);
	}

	// `x as T`: a checked cast of a pointer to an object (null when the dynamic type is not a `T`). An upcast
	// is a plain conversion; a downcast or cross-cast reads the run-time type information.
	QueryResult<Value> ExprLowerer::ResolveSafeCast(const SafeCastNode* n)
	{
		auto target = m_Ctx.Fail(n, m_Ctx.queries().ResolveTypeCtx(*n->GetTargetType().Get(), m_Ctx.unitId, m_Ctx.scope));
		if (!target.Ok()) return QueryResult<Value>::Fail(target.error, target.message);
		auto operand = ResolveExpr(n->GetOperand().Get());
		if (!operand.Ok()) return operand;
		if (target.value->GetKind() == TypeKind::Reference)
			return ResolveCheckedReferenceCast(n, operand.value, static_cast<const ReferenceType*>(target.value));
		operand.value = ThisAsPointer(operand.value);
		const Type* from = TypeHandle::UnwrapToValue(operand.value.type);
		const Type* to = TypeHandle::UnwrapToValue(target.value);
		if (from->IsNumeric() && to->IsNumeric())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
				"'as' converts pointers to objects; for a numeric conversion use a cast: (" + to->GetName() + ")x"));
		return ResolveDynamicCast(n, operand.value, to, false);
	}

	QueryResult<Value> ExprLowerer::ResolveDynamicCast(const AstNode* n, const Value& operand, const Type* to, bool testOnly)
	{
		const Type* from = TypeHandle::UnwrapToValue(operand.type);
		const auto* toObj = to->GetKind() == TypeKind::Pointer ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(TypeHandle::GetPointedType(to))) : nullptr;
		const auto* fromObj = from->GetKind() == TypeKind::Pointer ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(TypeHandle::GetPointedType(from))) : nullptr;
		if (!toObj || !fromObj)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
				"a checked cast needs pointers to objects: '" + from->GetName() + "' to '" + to->GetName() + "'"));
		const HIRNode* src = m_Ctx.LoadIfNeeded(operand);
		if (ClassHierarchy::DerivesFrom(fromObj, toObj) && !testOnly)
			return Value::R(m_Ctx.out->NewNode<ConvertNode>(src, src->type, to), to); // an upcast always succeeds
		if (!fromObj->IsPolymorphic())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
				"cannot check the dynamic type of '" + fromObj->GetName() + "': it has no virtual methods"));
		const Type* result = testOnly ? m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool) : to;
		return Value::R(m_Ctx.out->NewNode<DynamicCastNode>(result, src, toObj, testOnly), result);
	}

	QueryResult<Value> ExprLowerer::ResolveCast(const CastExpressionNode* n)
	{
		auto target = m_Ctx.Fail(n, m_Ctx.queries().ResolveTypeCtx(*n->GetTargetType().Get(), m_Ctx.unitId, m_Ctx.scope));
		if (!target.Ok()) return QueryResult<Value>::Fail(target.error, target.message);
		auto operand = ResolveExpr(n->GetOperand().Get());
		if (!operand.Ok()) return operand;
		if (TypeHandle::UnwrapToValue(target.value)->GetKind() == TypeKind::Pointer) operand.value = ThisAsPointer(operand.value);

		// A [operator("cast")] method overrides the built-in conversion: `(float)a` calls a.ToFloat() and
		// `(OpIVec2)5.5f` calls the primitive-receiver ToIVec2() (this = &value). The method's RETURN type IS
		// the cast target, so its result is the value - no further conversion.
		if (const FunctionRecord* castRec = m_Ctx.queries().FindCastMethod(operand.value.type, target.value))
		{
			if (auto allowed = AccessCheck::CheckCall(m_Ctx, *castRec); !allowed.Ok())
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));
			const Type* selfType = TypeHandle::UnwrapToValue(operand.value.type);
			const auto* obj = dynamic_cast<const ObjectType*>(selfType);
			if (obj)
			{
				// Object receiver: the operand object's location is `this`. It must be an lvalue.
				if (!operand.value.isLValue)
					return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "cast object must be an lvalue"));
				auto call = m_Ctx.out->NewNode<CallNode>(target.value, castRec);
				if (auto bound = BindReceiver(m_Ctx, call, m_Ctx.NewLValue(*operand.value.lvalue)); !bound.Ok())
					return m_Ctx.Fail(n, QueryResult<Value>::Fail(bound.error, bound.message));
				return Value::R(call, target.value);
			}
			// Primitive receiver: `this` is the ADDRESS of the value. Materialize the operand into a temp slot,
			// pass that slot (its address) as the receiver, then yield the result - one evaluation, correct order.
			uint32 tmp = m_Ctx.AllocSlot(selfType, "__cast_receiver", SlotClass::Local);
			LValue tmpLv; tmpLv.kind = LValue::Kind::Slot; tmpLv.type = selfType; tmpLv.slot = tmp;
			const HIRNode* storeTmp = m_Ctx.out->NewNode<StoreNode>(m_Ctx.NewLValue(tmpLv), m_Ctx.LoadIfNeeded(operand.value));
			auto call = m_Ctx.out->NewNode<CallNode>(target.value, castRec);
			call->receiver = m_Ctx.NewLValue(tmpLv); // this = &temp (the value's address)
			// The cast method RETURNS the target value directly, so the sequence is [store operand into temp,
			// call with this=&temp] and the call IS the result. One evaluation of the operand, correct order.
			auto seq = m_Ctx.out->NewNode<SequenceNode>(target.value);
			seq->steps.push_back(storeTmp); // evaluate the operand once into the temp
			seq->steps.push_back(call);     // run the cast (this = &temp); yields the target value
			return Value::R(seq, target.value);
		}

		if (target.value->GetKind() == TypeKind::Reference)
			return ResolveReferenceCast(n, operand.value, static_cast<const ReferenceType*>(target.value));
		if (n->GetKind() == CastExpressionNode::CastKind::DynamicCast)
			return ResolveDynamicCast(n, operand.value, target.value, false);
		if (Core::String why = CastRules::WhyRejected(n->GetKind(), operand.value.type, target.value); !why.empty())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));
		const bool keepsBits = CastRules::KeepsBits(n->GetKind(), operand.value.type, target.value);
		if (const auto* lanes = dynamic_cast<const SwizzleType*>(TypeHandle::UnwrapToValue(target.value)))
			return m_Ctx.Fail(n, VectorCast(m_Ctx.LoadIfNeeded(operand.value), lanes, keepsBits));
		auto* conv = m_Ctx.out->NewNode<ConvertNode>(m_Ctx.LoadIfNeeded(operand.value), operand.value.type, target.value);
		conv->keepsBits = keepsBits;
		return Value::R(conv, target.value);
	}

	// D-62: a number broadcasts into every lane; reinterpret_cast keeps a vector's bits; any other cast converts lane by
	// lane. A pure packed vector object is read as its lanes first.
	QueryResult<Value> ExprLowerer::VectorCast(const HIRNode* value, const SwizzleType* target, bool keepsBits)
	{
		const SimdOperands::Operand source = SimdOperands::Read(m_Ctx, Value::R(value, value->type));
		if (!source.lanes)
		{
			auto broadcast = ImplicitConversion::Scalar(m_Ctx, value, target);
			if (!broadcast.Ok()) return QueryResult<Value>::Fail(broadcast.error, broadcast.message);
			return Value::R(broadcast.value, target);
		}
		if (source.lanes == target) return Value::R(source.node, target);
		auto* conversion = m_Ctx.out->NewNode<ConvertNode>(source.node, source.lanes, target);
		conversion->keepsBits = keepsBits;
		return Value::R(conversion, target);
	}
}
