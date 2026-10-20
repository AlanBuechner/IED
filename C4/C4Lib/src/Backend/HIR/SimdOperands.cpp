#include "Backend/HIR/SimdOperands.h"
#include "Backend/HIR/ConstantOperands.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/PointerType.h"

namespace Scripting::SimdOperands
{
	namespace
	{
		const SwizzleType* Canonical(LoweringContext& ctx, const SwizzleType* lanes)
		{
			return static_cast<const SwizzleType*>(ctx.queries().TypeStore()->Swizzle(lanes->GetElementType(), lanes->GetLaneCount()));
		}

		Core::String Arguments(size_t count) { return std::to_string(count) + (count == 1 ? " argument" : " arguments"); }
	}

	Result Fail(const Core::String& message) { return Result::Fail(QueryError::NotAValue, message); }

	Core::String Quoted(const Type* t) { return "'" + Core::String(t ? t->GetName() : "?") + "'"; }

	Operand Read(LoweringContext& ctx, const Value& value)
	{
		Operand out;
		out.node = ctx.LoadIfNeeded(value);
		out.type = TypeHandle::UnwrapToValue(value.type);
		if (const auto* vector = dynamic_cast<const SwizzleType*>(out.type); vector && vector->IsPacked())
			out.lanes = Canonical(ctx, vector);
		else if (const auto* object = dynamic_cast<const ObjectType*>(out.type); object && object->IsPurePackedVector())
		{
			out.lanes = Canonical(ctx, object->GetPackedVectorSwizzle());
			out.node = ctx.out->NewNode<ConvertNode>(out.node, out.type, out.lanes); // the same register value, read as its lanes
		}
		return out;
	}

	QueryResult<const SwizzleType*> Shape(LoweringContext& ctx, const Type* element, uint64 lanes)
	{
		if (Core::String why = SwizzleType::WhyNotAVector(element, lanes, 2); !why.empty())
			return QueryResult<const SwizzleType*>::Fail(QueryError::NotAValue, why);
		return static_cast<const SwizzleType*>(ctx.queries().TypeStore()->Swizzle(element, static_cast<uint32>(lanes)));
	}

	bool IsNumber(const Type* t)
	{
		return t && t->IsNumeric() && t->GetKind() != TypeKind::Bool && t->GetKind() != TypeKind::Enum && !t->IsNormalized();
	}

	bool IsInteger(const Type* t) { return IsNumber(t) && t->IsIntegral(); }

	bool IsMask(const SwizzleType* lanes) { return lanes && lanes->GetElementType()->GetKind() == TypeKind::Bool; }

	Core::String ArgumentCount(const Call& call, size_t wanted)
	{
		if (call.args.size() == wanted) return "";
		return call.name + " takes " + Arguments(wanted) + ", not " + std::to_string(call.args.size());
	}

	QueryResult<int64> Constant(const Call& call, size_t index, const Core::String& what)
	{
		if (std::optional<int64> value = ConstantOperands::IntegerOf(call.ctx.LoadIfNeeded(call.args[index]))) return *value;
		return QueryResult<int64>::Fail(QueryError::NotAValue, call.name + "'s " + what + " is not a constant integer: it decides the vector's shape");
	}

	QueryResult<const HIRNode*> LanePointer(const Call& call, size_t index, bool writes)
	{
		using Pointed = QueryResult<const HIRNode*>;
		const Value& value = call.args[index];
		const Type* type = TypeHandle::UnwrapToValue(value.type);
		const HIRNode* node = call.ctx.LoadIfNeeded(value);
		if (const auto* array = dynamic_cast<const ArrayType*>(type))
		{
			const Type* element = TypeHandle::UnwrapToValue(array->GetElementType());
			type = call.ctx.queries().TypeStore()->Pointer(element);
			node = call.ctx.DecayArray(node, type);
		}
		const auto* pointer = dynamic_cast<const PointerType*>(type);
		if (!pointer || !node) return Pointed::Fail(QueryError::NotAValue, call.name + " takes a pointer to the lanes in memory, not a " + Quoted(type));
		const Type* element = TypeHandle::UnwrapToValue(pointer->GetPointeeType());
		if (!IsNumber(element))
			return Pointed::Fail(QueryError::NotAValue, call.name + " works on number lanes, and " + Quoted(type) + " points to a " + Quoted(element));
		if (writes && pointer->IsConstReferent()) return Pointed::Fail(QueryError::NotAValue, call.name + " writes, but " + Quoted(type) + " points to const");
		return node;
	}

	SimdNode* MakeSimd(const Call& call, const Type* type, SimdNode::Op op, std::vector<const HIRNode*> operands)
	{
		auto* node = call.ctx.out->NewNode<SimdNode>(type, op);
		node->operands = std::move(operands);
		return node;
	}

	Value Node(SimdNode* node) { return Value::R(node, node->type); }
} // namespace Scripting::SimdOperands
