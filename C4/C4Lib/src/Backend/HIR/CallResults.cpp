#include "Backend/HIR/CallResults.h"
#include "Model/Types/ReferenceType.h"

namespace Scripting
{
	Value CallResult(LoweringContext& ctx, const HIRNode* call, const Type* returned)
	{
		if (!returned || returned->GetKind() != TypeKind::Reference) return Value::R(call, returned);
		LValue referent;
		referent.kind = LValue::Kind::Deref;
		referent.type = static_cast<const ReferenceType*>(returned)->GetReferentType();
		referent.addr = call;
		return Value::L(ctx.NewLValue(referent));
	}
} // namespace Scripting
