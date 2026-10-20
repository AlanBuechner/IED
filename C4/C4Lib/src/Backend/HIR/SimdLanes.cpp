#include "Backend/HIR/SimdLanes.h"

namespace Scripting::SimdLanes
{
	using namespace SimdOperands;

	namespace
	{
		using Op = SimdNode::Op;

		// The shuffle's sources: one vector, or two of the same shape.
		struct Sources
		{
			std::vector<Operand> vectors;
			const SwizzleType* lanes = nullptr;
		};

		QueryResult<Sources> ShuffleSources(const Call& call)
		{
			using Found = QueryResult<Sources>;
			Sources sources;
			sources.vectors.push_back(Read(call.ctx, call.args[0]));
			sources.lanes = sources.vectors[0].lanes;
			if (!sources.lanes)
				return Found::Fail(QueryError::NotAValue, call.name + " takes a packed vector first, not a " + Quoted(sources.vectors[0].type));
			Operand second = Read(call.ctx, call.args[1]);
			if (!second.lanes) return sources;
			if (second.lanes != sources.lanes)
				return Found::Fail(QueryError::NotAValue, call.name + "'s two vectors are a " + Quoted(sources.lanes) + " and a " + Quoted(second.lanes) +
					": a shuffle takes lanes from vectors of one shape");
			sources.vectors.push_back(second);
			return sources;
		}

		Core::String IndexRange(const Sources& sources)
		{
			const uint32 lanes = sources.lanes->GetLaneCount();
			if (sources.vectors.size() == 1) return "lanes 0 to " + std::to_string(lanes - 1);
			return "lanes 0 to " + std::to_string(2 * lanes - 1) + ", the second vector's from " + std::to_string(lanes);
		}

		QueryResult<std::vector<int32>> ShuffleIndices(const Call& call, const Sources& sources)
		{
			using Indices = QueryResult<std::vector<int32>>;
			const int64 laneCount = static_cast<int64>(sources.lanes->GetLaneCount() * sources.vectors.size());
			std::vector<int32> indices;
			for (size_t i = sources.vectors.size(); i < call.args.size(); ++i)
			{
				auto index = Constant(call, i, "lane index at position " + std::to_string(indices.size() + 1));
				if (!index.Ok()) return Indices::Fail(index.error, index.message);
				if (index.value < 0 || index.value >= laneCount)
					return Indices::Fail(QueryError::NotAValue, call.name + "'s lane index " + std::to_string(index.value) + " is out of range: the vectors have " +
						IndexRange(sources));
				indices.push_back(static_cast<int32>(index.value));
			}
			return indices;
		}
	}

	Result LowerShuffle(const Call& call)
	{
		if (call.args.size() < 2) return Fail(call.name + " takes a vector and its lane indices: simd_shuffle(v, 3, 2, 1, 0)");
		auto sources = ShuffleSources(call);
		if (!sources.Ok()) return Result::Fail(sources.error, sources.message);
		auto indices = ShuffleIndices(call, sources.value);
		if (!indices.Ok()) return Result::Fail(indices.error, indices.message);
		if (indices.value.size() < 2) return Fail(call.name + " picks at least two lanes: for one lane, index the vector (v[3])");
		auto shape = Shape(call.ctx, sources.value.lanes->GetElementType(), indices.value.size());
		if (!shape.Ok()) return Fail(call.name + " makes a vector of " + std::to_string(indices.value.size()) + " lanes: " + shape.message);
		std::vector<const HIRNode*> operands;
		for (const Operand& vector : sources.value.vectors) operands.push_back(vector.node);
		SimdNode* node = MakeSimd(call, shape.value, Op::Shuffle, std::move(operands));
		node->indices = std::move(indices.value);
		return Node(node);
	}

	Result LowerConcat(const Call& call)
	{
		if (Core::String why = ArgumentCount(call, 2); !why.empty()) return Fail(why);
		const Operand low = Read(call.ctx, call.args[0]);
		const Operand high = Read(call.ctx, call.args[1]);
		if (!low.lanes || low.lanes != high.lanes)
			return Fail(call.name + " joins two packed vectors of one shape, not a " + Quoted(low.type) + " and a " + Quoted(high.type));
		auto shape = Shape(call.ctx, low.lanes->GetElementType(), 2ull * low.lanes->GetLaneCount());
		if (!shape.Ok()) return Fail(call.name + " of two " + Quoted(low.lanes) + ": " + shape.message);
		return Node(MakeSimd(call, shape.value, Op::Concat, { low.node, high.node }));
	}

	Result LowerSplat(const Call& call)
	{
		if (Core::String why = ArgumentCount(call, 2); !why.empty()) return Fail(why);
		const Operand value = Read(call.ctx, call.args[0]);
		if (value.lanes) return Fail(call.name + " takes a scalar: to copy one lane of a vector to every lane, write simd_shuffle(v, i, i, ...)");
		if (!IsNumber(value.type) && !(value.type && value.type->GetKind() == TypeKind::Bool))
			return Fail(call.name + " fills lanes with a number or a bool, not a " + Quoted(value.type));
		auto lanes = Constant(call, 1, "lane count");
		if (!lanes.Ok()) return Result::Fail(lanes.error, lanes.message);
		auto shape = Shape(call.ctx, value.type, static_cast<uint64>(lanes.value < 0 ? 0 : lanes.value));
		if (!shape.Ok()) return Fail(shape.message);
		return Node(MakeSimd(call, shape.value, Op::Splat, { value.node }));
	}
} // namespace Scripting::SimdLanes
