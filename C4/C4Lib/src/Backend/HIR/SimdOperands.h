#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Model/Types/SwizzleType.h"
#include <vector>

namespace Scripting
{
	/// What the SIMD intrinsic families share (D-62): reading arguments as lanes, the lane checks and the messages.
	namespace SimdOperands
	{
		using Result = QueryResult<Value>;

		/// One argument: its rvalue and, for a vector (a `packed T[N]` or a pure packed vector object), its lanes as
		/// the canonical `packed T[N]`, which is then the rvalue's type too. A scalar has no lanes.
		struct Operand
		{
			const HIRNode* node = nullptr;
			const SwizzleType* lanes = nullptr;
			const Type* type = nullptr;
		};

		/// The intrinsic call being lowered.
		struct Call
		{
			LoweringContext& ctx;
			const Core::String& name;
			const std::vector<Value>& args;
		};

		Result Fail(const Core::String& message);
		Core::String Quoted(const Type* t);

		Operand Read(LoweringContext& ctx, const Value& value);
		/// `packed element[lanes]`, or why that is not a vector.
		QueryResult<const SwizzleType*> Shape(LoweringContext& ctx, const Type* element, uint64 lanes);

		/// A number lane: an integer or a float, not a bool or a normalized integer.
		bool IsNumber(const Type* t);
		bool IsInteger(const Type* t);
		bool IsMask(const SwizzleType* lanes);

		/// "simd_min takes 2 arguments, not 3" when the call has another count than `wanted`; empty otherwise.
		Core::String ArgumentCount(const Call& call, size_t wanted);
		/// The constant integer of argument `index`, or why it is none (`what` names the argument).
		QueryResult<int64> Constant(const Call& call, size_t index, const Core::String& what);
		/// The pointer argument `index` read as a pointer to number lanes: an array decays to its first element.
		QueryResult<const HIRNode*> LanePointer(const Call& call, size_t index, bool writes);

		/// A `type` SimdNode of `op` over `operands`, and the value it is.
		SimdNode* MakeSimd(const Call& call, const Type* type, SimdNode::Op op, std::vector<const HIRNode*> operands);
		Value Node(SimdNode* node);
	}
} // namespace Scripting
