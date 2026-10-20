#include "Backend/HIR/MethodReceiver.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/VirtualTables.h"

namespace Scripting
{
	const LValue* SubobjectLValue(LoweringContext& ctx, const LValue* base, const ObjectType* owner, int64 offset)
	{
		// At offset 0 the sub-object shares the address but not the type: a view of the base is still a Base. A member
		// with no owner (a named union's, an __init_list's) is in the base itself.
		if (!base || !owner || (offset == 0 && base->type == owner)) return base;
		LValue lv; lv.kind = LValue::Kind::Base; lv.type = owner; lv.base = base; lv.baseOffset = offset;
		lv.ephemeral = base->ephemeral;
		return ctx.NewLValue(lv);
	}

	const ObjectType::MethodInfo* MethodOf(LoweringContext& ctx, const FunctionRecord* rec)
	{
		return rec ? ctx.model().MethodForRecord(*rec) : nullptr;
	}

	QueryResult<void> BindReceiver(LoweringContext& ctx, CallNode* call, const LValue* receiver, CallDispatch dispatch)
	{
		call->receiver = receiver;
		const FunctionRecord* rec = call->callee;
		const ObjectType* owner = rec ? rec->containingType : nullptr;
		const auto* recvType = receiver ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(receiver->type)) : nullptr;
		if (!owner || !recvType || rec->isStatic) return QueryResult<void>::Success();

		const ObjectType::MethodInfo* method = dispatch == CallDispatch::Dynamic ? MethodOf(ctx, rec) : nullptr;
		if (method)
		{
			VirtualTables::Dispatch d = VirtualTables::DispatchFor(recvType, owner, method);
			if (d.isVirtual)
			{
				call->receiver = SubobjectLValue(ctx, receiver, owner, d.vptrOffset);
				call->virtualIndex = d.index;
				return QueryResult<void>::Success();
			}
		}
		ClassHierarchy::BaseLocation at = ClassHierarchy::LocateBase(recvType, owner);
		if (at.ambiguous)
			return QueryResult<void>::Fail(QueryError::Ambiguous, "'" + rec->name + "' is ambiguous: '" + recvType->GetName() +
				"' contains more than one '" + owner->GetName() + "'");
		if (at.found) call->receiver = SubobjectLValue(ctx, receiver, owner, at.offset);
		return QueryResult<void>::Success();
	}
} // namespace Scripting
