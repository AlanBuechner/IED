#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/IndexLValue.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ReferenceType.h"

namespace Scripting
{
	namespace
	{
		const LValue* Container(const LValue* lv)
		{
			switch (lv->kind)
			{
			case LValue::Kind::Field:
			case LValue::Kind::Base:
			case LValue::Kind::Lane:
			case LValue::Kind::Swizzle:
			case LValue::Kind::VectorLaneDyn: return lv->base;
			case LValue::Kind::IndexOp: return lv->receiver;
			default: return nullptr;
			}
		}

		const LValue* Root(const LValue* lv)
		{
			while (const LValue* up = Container(lv)) lv = up;
			return lv;
		}

		bool PointsAtConst(const HIRNode* address)
		{
			const Type* t = address ? address->type : nullptr;
			while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
			if (!t) return false;
			if (t->GetKind() == TypeKind::Pointer) return static_cast<const PointerType*>(t)->IsConstReferent();
			if (t->GetKind() == TypeKind::Reference) return static_cast<const ReferenceType*>(t)->IsConstReferent();
			return false;
		}

		// A create, copy or delete - written or compiler-written - works on an object that is not complete.
		bool BuildsOrEndsReceiver(const FunctionRecord* fn)
		{
			if (!fn || !fn->containingType || fn->isStatic) return false;
			if (fn->synthesized != LifecycleOp::None) return true;
			for (const Core::String& op : fn->operatorAnnotations)
				if (op == "create" || op == "copy" || op == "delete") return true;
			return false;
		}

		Core::String RootReason(const LoweringContext& ctx, const LValue* p)
		{
			switch (p->kind)
			{
			case LValue::Kind::Slot:
				return p->readOnly ? "'" + ctx.out->GetSlot(p->slot).name + "' is const" : "";
			case LValue::Kind::Global:
				return p->sym && p->sym->isConst ? "'" + p->sym->plainName + "' is const" : "";
			case LValue::Kind::Deref:
				if (const LValue* array = IndexedArray(p))
					if (Core::String why = ReadOnlyReason(ctx, array); !why.empty()) return why;
				return p->readOnly || PointsAtConst(p->addr) ? "it is reached through a pointer or reference to const" : "";
			case LValue::Kind::This:
				if (!ctx.fn || !ctx.fn->isConst) return "";
				return ctx.closureObject ? "the lambda is not mutable, so its copies of captured variables are read-only"
					: "'this' is const in the const method '" + ctx.fn->name + "'";
			default:
				return p->readOnly ? "it is read-only" : "";
			}
		}
	}

	Core::String ReadOnlyReason(const LoweringContext& ctx, const LValue* location)
	{
		const LValue* root = Root(location);
		const bool receiverInProgress = root->kind == LValue::Kind::This && !ctx.closureObject && BuildsOrEndsReceiver(ctx.fn);
		for (const LValue* p = location; p; p = Container(p))
			if (p->kind == LValue::Kind::Field && p->field && p->field->m_IsConst && !receiverInProgress)
				return "field '" + p->field->mName + "' is const";
		return RootReason(ctx, root);
	}

	const Type* AddressType(LoweringContext& ctx, const LValue* location)
	{
		return ctx.queries().TypeStore()->Pointer(location->type, IsReadOnly(ctx, location));
	}
} // namespace Scripting
