#include "Backend/HIR/SimdMemory.h"
#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"

namespace Scripting::SimdMemory
{
	using namespace SimdOperands;

	namespace
	{
		using Op = SimdNode::Op;

		const Type* PointedElement(const HIRNode* pointer)
		{
			const auto* type = static_cast<const PointerType*>(TypeHandle::UnwrapToValue(pointer->type));
			return TypeHandle::UnwrapToValue(type->GetPointeeType());
		}

		size_t Arity(Op op) { return op == Op::StoreMasked || op == Op::GatherMasked ? 3 : 2; }

		bool Writes(Op op) { return op == Op::Store || op == Op::StoreMasked; }

		// Argument `index` as a mask of `lanes` bool lanes.
		QueryResult<Operand> MaskOf(const Call& call, size_t index, uint32 lanes)
		{
			using Masked = QueryResult<Operand>;
			Operand mask = Read(call.ctx, call.args[index]);
			if (!IsMask(mask.lanes))
				return Masked::Fail(QueryError::NotAValue, call.name + " takes a mask, a 'packed bool[N]' (a comparison of vectors), not a " + Quoted(mask.type));
			if (lanes && mask.lanes->GetLaneCount() != lanes)
				return Masked::Fail(QueryError::NotAValue, call.name + "'s mask has " + std::to_string(mask.lanes->GetLaneCount()) + " lanes and its vector " +
					std::to_string(lanes) + ": they need one mask lane per vector lane");
			return mask;
		}


		Result Load(const Call& call, const HIRNode* pointer)
		{
			auto lanes = Constant(call, 1, "lane count");
			if (!lanes.Ok()) return Result::Fail(lanes.error, lanes.message);
			auto shape = Shape(call.ctx, PointedElement(pointer), static_cast<uint64>(lanes.value < 0 ? 0 : lanes.value));
			if (!shape.Ok()) return Fail(call.name + ": " + shape.message);
			return Node(MakeSimd(call, shape.value, Op::Load, { pointer }));
		}

		Result LoadMasked(const Call& call, const HIRNode* pointer)
		{
			auto mask = MaskOf(call, 1, 0);
			if (!mask.Ok()) return Result::Fail(mask.error, mask.message);
			auto shape = Shape(call.ctx, PointedElement(pointer), mask.value.lanes->GetLaneCount());
			if (!shape.Ok()) return Fail(call.name + ": " + shape.message);
			return Node(MakeSimd(call, shape.value, Op::LoadMasked, { pointer, mask.value.node }));
		}

		Result Store(const Call& call, const HIRNode* pointer, Op op)
		{
			const Operand vector = Read(call.ctx, call.args[1]);
			if (!vector.lanes) return Fail(call.name + " stores a packed vector, not a " + Quoted(vector.type));
			if (vector.lanes->GetElementType() != PointedElement(pointer))
				return Fail(call.name + " cannot store " + Quoted(vector.lanes) + " lanes through a " + Quoted(pointer->type) + ": the element types differ");
			std::vector<const HIRNode*> operands = { pointer, vector.node };
			if (op == Op::StoreMasked)
			{
				auto mask = MaskOf(call, 2, vector.lanes->GetLaneCount());
				if (!mask.Ok()) return Result::Fail(mask.error, mask.message);
				operands.push_back(mask.value.node);
			}
			return Node(MakeSimd(call, call.ctx.queries().TypeStore()->Builtin(TypeKind::Void), op, std::move(operands)));
		}

		Result Gather(const Call& call, const HIRNode* pointer, Op op)
		{
			const Operand indices = Read(call.ctx, call.args[1]);
			if (!indices.lanes || !IsInteger(indices.lanes->GetElementType()))
				return Fail(call.name + " takes the element indices as integer lanes, such as a 'packed int32[N]', not a " + Quoted(indices.type));
			auto shape = Shape(call.ctx, PointedElement(pointer), indices.lanes->GetLaneCount());
			if (!shape.Ok()) return Fail(call.name + ": " + shape.message);
			std::vector<const HIRNode*> operands = { pointer, indices.node };
			if (op == Op::GatherMasked)
			{
				auto mask = MaskOf(call, 2, indices.lanes->GetLaneCount());
				if (!mask.Ok()) return Result::Fail(mask.error, mask.message);
				operands.push_back(mask.value.node);
			}
			return Node(MakeSimd(call, shape.value, op, std::move(operands)));
		}
	}

	Result Lower(const Call& call, Op op)
	{
		if (Core::String why = ArgumentCount(call, Arity(op)); !why.empty()) return Fail(why);
		auto pointer = LanePointer(call, 0, Writes(op));
		if (!pointer.Ok()) return Result::Fail(pointer.error, pointer.message);
		switch (op)
		{
		case Op::Load: return Load(call, pointer.value);
		case Op::LoadMasked: return LoadMasked(call, pointer.value);
		case Op::Gather: case Op::GatherMasked: return Gather(call, pointer.value, op);
		default: return Store(call, pointer.value, op);
		}
	}
} // namespace Scripting::SimdMemory
