#include "Backend/HIR/SimdIntrinsics.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/SimdLanes.h"
#include "Backend/HIR/SimdMemory.h"

namespace Scripting::SimdIntrinsics
{
	using namespace SimdOperands;

	namespace
	{
		using Op = SimdNode::Op;

		enum class Family { LaneMath, Select, MaskTest, Reduce, Shuffle, Concat, Splat, Memory };
		enum class LaneKind { Any, Numbers, Floats, Integers };

		struct Form
		{
			Op op;
			Family family;
			size_t arity = 0;            // lane math and reductions; the other families check their own
			LaneKind lanes = LaneKind::Any;
		};

		bool Lookup(const Core::String& name, Form& out)
		{
			struct Entry { const char* name; Form form; };
			static const Entry kForms[] = {
				{ "simd_min", { Op::Min, Family::LaneMath, 2, LaneKind::Numbers } },
				{ "simd_max", { Op::Max, Family::LaneMath, 2, LaneKind::Numbers } },
				{ "simd_abs", { Op::Abs, Family::LaneMath, 1, LaneKind::Numbers } },
				{ "simd_sqrt", { Op::Sqrt, Family::LaneMath, 1, LaneKind::Floats } },
				{ "simd_floor", { Op::Floor, Family::LaneMath, 1, LaneKind::Floats } },
				{ "simd_ceil", { Op::Ceil, Family::LaneMath, 1, LaneKind::Floats } },
				{ "simd_round", { Op::Round, Family::LaneMath, 1, LaneKind::Floats } },
				{ "simd_trunc", { Op::Trunc, Family::LaneMath, 1, LaneKind::Floats } },
				{ "simd_muladd", { Op::MulAdd, Family::LaneMath, 3, LaneKind::Floats } },
				{ "simd_fma", { Op::Fma, Family::LaneMath, 3, LaneKind::Floats } },
				{ "simd_select", { Op::Select, Family::Select } },
				{ "simd_any", { Op::Any, Family::MaskTest } },
				{ "simd_all", { Op::All, Family::MaskTest } },
				{ "simd_bitmask", { Op::Bitmask, Family::MaskTest } },
				{ "simd_shuffle", { Op::Shuffle, Family::Shuffle } },
				{ "simd_concat", { Op::Concat, Family::Concat } },
				{ "simd_splat", { Op::Splat, Family::Splat } },
				{ "simd_reduce_add", { Op::ReduceAdd, Family::Reduce, 1, LaneKind::Numbers } },
				{ "simd_reduce_mul", { Op::ReduceMul, Family::Reduce, 1, LaneKind::Numbers } },
				{ "simd_reduce_min", { Op::ReduceMin, Family::Reduce, 1, LaneKind::Numbers } },
				{ "simd_reduce_max", { Op::ReduceMax, Family::Reduce, 1, LaneKind::Numbers } },
				{ "simd_reduce_and", { Op::ReduceAnd, Family::Reduce, 1, LaneKind::Integers } },
				{ "simd_reduce_or", { Op::ReduceOr, Family::Reduce, 1, LaneKind::Integers } },
				{ "simd_reduce_xor", { Op::ReduceXor, Family::Reduce, 1, LaneKind::Integers } },
				{ "simd_load", { Op::Load, Family::Memory } },
				{ "simd_store", { Op::Store, Family::Memory } },
				{ "simd_load_masked", { Op::LoadMasked, Family::Memory } },
				{ "simd_store_masked", { Op::StoreMasked, Family::Memory } },
				{ "simd_gather", { Op::Gather, Family::Memory } },
				{ "simd_gather_masked", { Op::GatherMasked, Family::Memory } },
			};
			for (const Entry& e : kForms)
				if (name == e.name) { out = e.form; return true; }
			return false;
		}

		bool Accepts(LaneKind kind, const Type* element)
		{
			switch (kind)
			{
			case LaneKind::Numbers: return IsNumber(element);
			case LaneKind::Floats: return element->IsFloatingPoint();
			case LaneKind::Integers: return IsInteger(element);
			default: return true;
			}
		}

		const char* Described(LaneKind kind)
		{
			switch (kind)
			{
			case LaneKind::Floats: return "float or double lanes";
			case LaneKind::Integers: return "integer lanes";
			default: return "number lanes";
			}
		}

		Core::String LanesProblem(const Call& call, LaneKind kind, const SwizzleType* lanes)
		{
			if (Accepts(kind, lanes->GetElementType())) return "";
			return call.name + " works on " + Described(kind) + ", not a " + Quoted(lanes);
		}

		// The processor features an intrinsic needs (D-62): only simd_fma has no fallback.
		Core::String FeatureProblem(const Call& call, Op op)
		{
			if (op != Op::Fma || call.ctx.model().Target().Has("fma")) return "";
			return call.name + " needs the processor's fused multiply-add: build with -mfma or -march=x86-64-v3 (or newer), or use simd_muladd, "
				"which fuses where the target can";
		}

		// The shape the vector operands share; a scalar operand broadcasts to it.
		QueryResult<const SwizzleType*> CommonShape(const Call& call, const std::vector<Operand>& operands)
		{
			using Shaped = QueryResult<const SwizzleType*>;
			const SwizzleType* shape = nullptr;
			for (const Operand& operand : operands)
			{
				if (!operand.lanes) continue;
				if (shape && operand.lanes != shape)
					return Shaped::Fail(QueryError::NotAValue, call.name + "'s vectors are a " + Quoted(shape) + " and a " + Quoted(operand.lanes) +
						": they combine only with the same element and lane count");
				shape = operand.lanes;
			}
			if (!shape)
				return Shaped::Fail(QueryError::NotAValue, call.name + " works on packed vectors: pass at least one 'packed T[N]' (a scalar argument broadcasts)");
			return shape;
		}

		// Each operand as `shape`'s lanes: a vector as it is, a scalar converted to the element and broadcast.
		QueryResult<std::vector<const HIRNode*>> AsLanes(const Call& call, const std::vector<Operand>& operands, const SwizzleType* shape)
		{
			using Nodes = QueryResult<std::vector<const HIRNode*>>;
			std::vector<const HIRNode*> nodes;
			for (const Operand& operand : operands)
			{
				if (operand.lanes) { nodes.push_back(operand.node); continue; }
				auto broadcast = ImplicitConversion::Scalar(call.ctx, operand.node, shape);
				if (!broadcast.Ok()) return Nodes::Fail(broadcast.error, call.name + ": " + broadcast.message);
				nodes.push_back(broadcast.value);
			}
			return nodes;
		}

		std::vector<Operand> ReadAll(const Call& call, size_t first)
		{
			std::vector<Operand> operands;
			for (size_t i = first; i < call.args.size(); ++i) operands.push_back(Read(call.ctx, call.args[i]));
			return operands;
		}


		Result LowerLaneMath(const Call& call, const Form& form)
		{
			if (Core::String why = ArgumentCount(call, form.arity); !why.empty()) return Fail(why);
			if (Core::String why = FeatureProblem(call, form.op); !why.empty()) return Fail(why);
			const std::vector<Operand> operands = ReadAll(call, 0);
			auto shape = CommonShape(call, operands);
			if (!shape.Ok()) return Result::Fail(shape.error, shape.message);
			if (Core::String why = LanesProblem(call, form.lanes, shape.value); !why.empty()) return Fail(why);
			auto lanes = AsLanes(call, operands, shape.value);
			if (!lanes.Ok()) return Result::Fail(lanes.error, lanes.message);
			return Node(MakeSimd(call, shape.value, form.op, std::move(lanes.value)));
		}

		QueryResult<Operand> MaskArgument(const Call& call)
		{
			Operand mask = Read(call.ctx, call.args[0]);
			if (IsMask(mask.lanes)) return mask;
			return QueryResult<Operand>::Fail(QueryError::NotAValue, call.name + " takes a mask, a 'packed bool[N]' (a comparison of vectors), not a " +
				Quoted(mask.type));
		}

		// With two scalars to choose from, the result has the mask's lane count and the first scalar's type.
		QueryResult<const SwizzleType*> SelectShape(const Call& call, const std::vector<Operand>& choices, const SwizzleType* mask)
		{
			if (choices[0].lanes || choices[1].lanes) return CommonShape(call, choices);
			return Shape(call.ctx, choices[0].type, mask->GetLaneCount());
		}

		Result LowerSelect(const Call& call)
		{
			if (Core::String why = ArgumentCount(call, 3); !why.empty()) return Fail(why);
			auto mask = MaskArgument(call);
			if (!mask.Ok()) return Result::Fail(mask.error, mask.message);
			const std::vector<Operand> choices = ReadAll(call, 1);
			auto shape = SelectShape(call, choices, mask.value.lanes);
			if (!shape.Ok()) return Result::Fail(shape.error, call.name + ": " + shape.message);
			if (shape.value->GetLaneCount() != mask.value.lanes->GetLaneCount())
				return Fail(call.name + "'s mask has " + std::to_string(mask.value.lanes->GetLaneCount()) + " lanes and its choices " +
					std::to_string(shape.value->GetLaneCount()) + ": they need one mask lane per lane");
			auto lanes = AsLanes(call, choices, shape.value);
			if (!lanes.Ok()) return Result::Fail(lanes.error, lanes.message);
			lanes.value.insert(lanes.value.begin(), mask.value.node);
			return Node(MakeSimd(call, shape.value, Op::Select, std::move(lanes.value)));
		}

		Result LowerMaskTest(const Call& call, Op op)
		{
			if (Core::String why = ArgumentCount(call, 1); !why.empty()) return Fail(why);
			auto mask = MaskArgument(call);
			if (!mask.Ok()) return Result::Fail(mask.error, mask.message);
			const Type* result = call.ctx.queries().TypeStore()->Builtin(op == Op::Bitmask ? TypeKind::UInt32 : TypeKind::Bool);
			return Node(MakeSimd(call, result, op, { mask.value.node }));
		}

		Result LowerReduce(const Call& call, const Form& form)
		{
			if (Core::String why = ArgumentCount(call, 1); !why.empty()) return Fail(why);
			const Operand vector = Read(call.ctx, call.args[0]);
			if (!vector.lanes) return Fail(call.name + " reduces a packed vector, not a " + Quoted(vector.type));
			if (Core::String why = LanesProblem(call, form.lanes, vector.lanes); !why.empty()) return Fail(why);
			return Node(MakeSimd(call, vector.lanes->GetElementType(), form.op, { vector.node }));
		}
	}

	bool IsIntrinsic(const Core::String& name)
	{
		Form form;
		return Lookup(name, form);
	}

	QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args)
	{
		Form form;
		Lookup(name, form);
		const Call call{ ctx, name, args };
		switch (form.family)
		{
		case Family::LaneMath: return LowerLaneMath(call, form);
		case Family::Select: return LowerSelect(call);
		case Family::MaskTest: return LowerMaskTest(call, form.op);
		case Family::Reduce: return LowerReduce(call, form);
		case Family::Shuffle: return SimdLanes::LowerShuffle(call);
		case Family::Concat: return SimdLanes::LowerConcat(call);
		case Family::Splat: return SimdLanes::LowerSplat(call);
		default: return SimdMemory::Lower(call, form.op);
		}
	}
} // namespace Scripting::SimdIntrinsics
