#include "Backend/HIR/HintIntrinsics.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Model/TypeHandle.h"

namespace Scripting::HintIntrinsics
{
	namespace
	{
		using Result = QueryResult<Value>;

		Result Fail(const Core::String& message) { return Result::Fail(QueryError::NotAValue, message); }

		const Type* Builtin(LoweringContext& ctx, TypeKind kind) { return ctx.queries().TypeStore()->Builtin(kind); }

		// `likely(c)` / `unlikely(c)`: the condition, as a bool, with the way it usually goes.
		Result Expect(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args, IntrinsicOp op)
		{
			if (args.size() != 1 || (!args[0].isLValue && !args[0].rvalue)) return Fail(name + "(c) takes one condition");
			const Type* boolType = Builtin(ctx, TypeKind::Bool);
			auto condition = ImplicitConversion::Scalar(ctx, ctx.LoadIfNeeded(args[0]), boolType);
			if (!condition.Ok()) return Fail(name + "(c) takes a condition: " + condition.message);
			return Value::R(ctx.out->NewNode<IntrinsicOpNode>(boolType, op, condition.value, nullptr), boolType);
		}

		// `mem_prefetch(p)`: any pointer; nothing comes back.
		Result Prefetch(LoweringContext& ctx, const std::vector<Value>& args)
		{
			const HIRNode* pointer = args.size() == 1 && (args[0].isLValue || args[0].rvalue) ? ctx.LoadIfNeeded(args[0]) : nullptr;
			const Type* t = pointer ? TypeHandle::UnwrapToValue(pointer->type) : nullptr;
			if (!t || t->GetKind() != TypeKind::Pointer) return Fail("mem_prefetch(p) takes one pointer to the memory about to be read");
			const Type* voidType = Builtin(ctx, TypeKind::Void);
			return Value::R(ctx.out->NewNode<IntrinsicOpNode>(voidType, IntrinsicOp::Prefetch, pointer, nullptr), voidType);
		}
	}

	bool IsIntrinsic(const Core::String& name) { return name == "likely" || name == "unlikely" || name == "mem_prefetch"; }

	QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args)
	{
		if (name == "mem_prefetch") return Prefetch(ctx, args);
		return Expect(ctx, name, args, name == "likely" ? IntrinsicOp::Likely : IntrinsicOp::Unlikely);
	}
} // namespace Scripting::HintIntrinsics
