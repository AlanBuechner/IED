#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// `base` viewed as the `owner` sub-object at `offset` inside it (no wrapper when it already is one).
	const LValue* SubobjectLValue(LoweringContext& ctx, const LValue* base, const ObjectType* owner, int64 offset);

	/// The MethodInfo behind a method record, or null (free functions, extension methods).
	const ObjectType::MethodInfo* MethodOf(LoweringContext& ctx, const FunctionRecord* rec);

	/// Dynamic: a virtual method dispatches through the receiver's vtable. Direct: the call names one
	/// implementation (`Base::F()`) and never dispatches.
	enum class CallDispatch { Dynamic, Direct };

	/// Set `call`'s receiver: `receiver` adjusted to the sub-object that declares the callee and, for a dynamic
	/// call of a virtual method, to the vptr the call dispatches through with its slot index. Fails when the
	/// declaring type is more than one sub-object of the receiver.
	QueryResult<void> BindReceiver(LoweringContext& ctx, CallNode* call, const LValue* receiver, CallDispatch dispatch = CallDispatch::Dynamic);
} // namespace Scripting
