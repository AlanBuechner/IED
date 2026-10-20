#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/Types/PointerType.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/SwizzleType.h"
#include "Model/ConstEvaluator.h"
#include <cmath>
#include <limits>

namespace Scripting::ImplicitConversion
{
	namespace
	{
		using Result = QueryResult<const HIRNode*>;

		bool PointerLike(const Type* t) { return t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr; }
		bool Aggregate(const Type* t) { return t->GetKind() == TypeKind::Object || t->GetKind() == TypeKind::Array || t->GetKind() == TypeKind::Union; }
		bool ScopedEnum(const Type* t) { return t->GetKind() == TypeKind::Enum && static_cast<const EnumType*>(t)->IsScoped(); }
		Core::String Quoted(const Type* t) { return "'" + t->GetName() + "'"; }

		bool IsZeroConstant(const HIRNode* value)
		{
			const auto* constant = dynamic_cast<const ConstantNode*>(value);
			return constant && constant->type && constant->type->IsIntegral() && constant->value.kind == ConstantValue::Kind::Int64 &&
				constant->value.intValue == 0;
		}

		// D-32: an enum class converts from and to integers (and other enums) only through a cast.
		Core::String EnumProblem(const Type* from, const Type* to)
		{
			if (ScopedEnum(to)) return Quoted(to) + " is an enum class: a " + Quoted(from) + " converts to it only with a cast, (" + to->GetName() + ")x";
			if (ScopedEnum(from)) return Quoted(from) + " is an enum class: it converts to " + Quoted(to) + " only with a cast, (" + to->GetName() + ")x";
			if (from->GetKind() == TypeKind::Enum && to->GetKind() == TypeKind::Enum) return "a " + Quoted(from) + " is not a " + Quoted(to);
			return "";
		}

		Result ToPointer(LoweringContext& ctx, const HIRNode* value, const Type* from, const Type* to)
		{
			if (PointerLike(from)) return Result(ctx.CoerceValue(value, to));
			if (const HIRNode* decayed = ctx.DecayArray(value, to)) return Result(decayed);
			// A braced list passed to a `__init_list<T>*` (C++'s `const std::initializer_list<T>&`) is a temporary's address.
			if (from->GetKind() == TypeKind::InitList && to->GetKind() == TypeKind::Pointer &&
				TypeHandle::UnwrapToValue(static_cast<const PointerType*>(to)->GetPointeeType()) == from)
				return Result(ctx.AddressAs(to, Lifecycle(ctx).Temporary(value)));
			if (IsZeroConstant(value)) return Result(ctx.out->NewNode<ConvertNode>(value, value->type, to));
			if (from->IsIntegral())
				return Result::Fail(QueryError::NotAValue, "an integer is not a pointer: write null for no address, or cast with (" + to->GetName() + ")");
			return Result::Fail(QueryError::NotAValue, "a " + Quoted(from) + " is not a pointer");
		}

		// A pointer's truth value: `!!p`, as a condition reads it.
		const HIRNode* TruthValue(LoweringContext& ctx, const HIRNode* value, const Type* boolType)
		{
			const HIRNode* isNull = ctx.out->NewNode<IntrinsicOpNode>(boolType, IntrinsicOp::BoolNot, value, nullptr);
			return ctx.out->NewNode<IntrinsicOpNode>(boolType, IntrinsicOp::BoolNot, isNull, nullptr);
		}

		Result ToNumber(LoweringContext& ctx, const HIRNode* value, const Type* from, const Type* to)
		{
			if (from->IsNumeric())
			{
				if (Core::String why = EnumProblem(from, to); !why.empty()) return Result::Fail(QueryError::NotAValue, why);
				return Result(ctx.out->NewNode<ConvertNode>(value, value->type, to));
			}
			if (PointerLike(from) && to->GetKind() == TypeKind::Bool) return Result(TruthValue(ctx, value, to));
			if (PointerLike(from)) // an address reads as an integer; a cast never makes one a float
				return Result::Fail(QueryError::NotAValue, "a pointer is not a number: cast with (" + Core::String(to->IsIntegral() ? to->GetName() : "uint64") +
					") to read its address");
			Core::String why = "a " + Quoted(from) + " is not a number";
			if (ctx.queries().FindCastMethod(from, to)) why += ": convert it with (" + to->GetName() + ")x";
			return Result::Fail(QueryError::NotAValue, why);
		}

		// A pure packed vector object of the same lanes is that vector (`v3.zx = v2`), the representation they share.
		bool SameLanes(const Type* from, const SwizzleType* to)
		{
			const auto* object = dynamic_cast<const ObjectType*>(from);
			const SwizzleType* lanes = object && object->IsPurePackedVector() ? object->GetPackedVectorSwizzle() : nullptr;
			return lanes && lanes->GetLaneCount() == to->GetLaneCount() && lanes->GetElementType() == to->GetElementType();
		}

		// A scalar converts to a packed vector by broadcasting to every lane, after converting to the element (D-62).
		Result ToPacked(LoweringContext& ctx, const HIRNode* value, const Type* from, const SwizzleType* to)
		{
			if (SameLanes(from, to)) return Result(ctx.out->NewNode<ConvertNode>(value, from, to));
			if (from->GetKind() == TypeKind::Swizzle)
				return Result::Fail(QueryError::NotAValue, "a " + Quoted(from) + " is not a " + Quoted(to) + ": convert the lanes with (" +
					to->GetName() + ")v");
			if (!from->IsNumeric()) return Result::Fail(QueryError::NotAValue, "a " + Quoted(from) + " is not a " + Quoted(to));
			Result lane = ToNumber(ctx, value, from, to->GetElementType());
			if (!lane.Ok()) return lane;
			auto* splat = ctx.out->NewNode<SimdNode>(to, SimdNode::Op::Splat);
			splat->operands.push_back(lane.value);
			return Result(splat);
		}

		bool IsPackedVector(const Type* t) { return t->GetKind() == TypeKind::Swizzle && static_cast<const SwizzleType*>(t)->IsPacked(); }
	}

	Result Scalar(LoweringContext& ctx, const HIRNode* value, const Type* target)
	{
		const Type* from = TypeHandle::UnwrapToValue(value->type);
		const Type* to = TypeHandle::UnwrapToValue(target);
		if (!from || !to || from == to) return Result(value);
		if (PointerLike(to)) return ToPointer(ctx, value, from, to);
		if (to->IsNumeric()) return ToNumber(ctx, value, from, to);
		if (IsPackedVector(to)) return ToPacked(ctx, value, from, static_cast<const SwizzleType*>(to));
		if (Aggregate(from)) return Result::Fail(QueryError::NotAValue, "a " + Quoted(from) + " is not a " + Quoted(to));
		return Result(ctx.CoerceValue(value, to)); // SIMD lanes and init lists keep their representation rules
	}

	namespace
	{
		bool PlainNumber(const Type* t) { return t->IsNumeric() && !t->IsNormalized() && t->GetKind() != TypeKind::Bool; }

		bool Narrows(const Type* from, const Type* to)
		{
			if (from->IsFloatingPoint()) return to->IsIntegral() || (to->IsFloatingPoint() && to->GetSize() < from->GetSize());
			return from->IsIntegral() && to->IsIntegral() && to->GetSize() < from->GetSize();
		}

		bool IntegerFits(double v, const Type* to)
		{
			if (v != std::floor(v)) return false;
			const double bits = (double)(to->GetSize() * 8);
			if (to->IsUnsignedInteger()) return v >= 0.0 && v < std::pow(2.0, bits);
			return v >= -std::pow(2.0, bits - 1.0) && v < std::pow(2.0, bits - 1.0);
		}

		// The value as a constant, when the node is one or the written expression folds.
		bool ConstantOf(LoweringContext& ctx, const AstNode* site, const Value& value, ConstantValue& out)
		{
			if (const auto* constant = value.isLValue ? nullptr : dynamic_cast<const ConstantNode*>(value.rvalue)) { out = constant->value; return true; }
			if (!site) return false;
			auto folded = ConstEvaluator(*ctx.queries().Model(), ctx.scope).Evaluate(site, ctx.unitId);
			if (!folded.Ok()) return false;
			out = folded.value;
			return true;
		}

		bool ConstantFits(const ConstantValue& value, const Type* to)
		{
			double v = 0.0;
			if (value.kind == ConstantValue::Kind::Int64) v = (double)value.intValue;
			else if (value.kind == ConstantValue::Kind::Float64) v = value.floatValue;
			else return false;
			if (to->IsIntegral()) return IntegerFits(v, to);
			return !std::isfinite(v) || std::fabs(v) <= (double)std::numeric_limits<float>::max();
		}
	}

	void WarnIfNarrowing(LoweringContext& ctx, const AstNode* site, const Value& value, const Type* target)
	{
		if (!value.type) return;
		const Type* from = TypeHandle::UnwrapToValue(value.type);
		const Type* to = TypeHandle::UnwrapToValue(target);
		if (to && IsPackedVector(to)) to = static_cast<const SwizzleType*>(to)->GetElementType(); // a broadcast converts to the lanes
		if (!from || !to || from == to || !PlainNumber(from) || !PlainNumber(to) || !Narrows(from, to)) return;
		ConstantValue constant;
		if (ConstantOf(ctx, site, value, constant) && ConstantFits(constant, to)) return;
		ctx.Warn(site, "the conversion from " + Quoted(from) + " to " + Quoted(to) + " may change the value: write (" + to->GetName() +
			")x if that is intended");
	}

	Core::String ObjectProblem(LoweringContext& ctx, const Type* from, const Type* to)
	{
		if (from == to) return "";
		const auto* toObject = dynamic_cast<const ObjectType*>(to);
		const auto* fromObject = dynamic_cast<const ObjectType*>(from);
		if (toObject && fromObject && ClassHierarchy::DerivesFrom(fromObject, toObject))
			return ClassHierarchy::LocateBase(fromObject, toObject).ambiguous
				? Quoted(to) + " is a base of " + Quoted(from) + " more than once" : "";
		// A pure packed vector and a swizzle run of the same lanes share their representation.
		if (toObject && toObject->IsPurePackedVector() && from->GetKind() == TypeKind::Swizzle)
		{
			const SwizzleType* lanes = toObject->GetPackedVectorSwizzle();
			const auto* run = static_cast<const SwizzleType*>(from);
			if (lanes && lanes->GetLaneCount() == run->GetLaneCount() && lanes->GetElementType() == run->GetElementType()) return "";
		}
		Core::String why = "a " + Quoted(from) + " is not a " + Quoted(to);
		if (toObject && !LifecycleFacts::Own(ctx.queries(), toObject).creates.empty()) why += ": construct one with " + to->GetName() + "(...)";
		return why;
	}

	Core::String ConditionProblem(LoweringContext& ctx, const Type* type)
	{
		if (TypeHandle::HasTruthValue(type)) return "";
		const Type* t = TypeHandle::UnwrapToValue(type);
		if (!t) return "this expression has no value to test";
		if (const auto* vector = dynamic_cast<const SwizzleType*>(t))
			return "a " + Quoted(t) + " has " + std::to_string(vector->GetLaneCount()) +
				" lanes, not one truth value: test one lane, such as v[0], or combine lanes with '&' and '|'";
		Core::String why = "a " + Quoted(t) + " has no truth value: a condition is a bool, a number, an enum or a pointer";
		if (ctx.queries().FindCastMethod(t, ctx.queries().TypeStore()->Builtin(TypeKind::Bool))) why += "; convert it with (bool)x";
		return why;
	}
} // namespace Scripting::ImplicitConversion
