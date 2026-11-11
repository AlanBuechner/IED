#include "Backend/HIR/ConditionalBranches.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/PointerConversion.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/PointerType.h"

namespace Scripting
{
	namespace
	{
		TypeKind ArithmeticKind(const Type* t)
		{
			const auto* e = dynamic_cast<const EnumType*>(t);
			return e && e->GetUnderlyingType() ? e->GetUnderlyingType()->GetKind() : t->GetKind();
		}

		bool IsVoidPointer(const Type* t)
		{
			const auto* p = dynamic_cast<const PointerType*>(t);
			return p && p->GetPointeeType() && p->GetPointeeType()->GetKind() == TypeKind::Void;
		}

		// `branch` (of type `from`) becomes a `target` implicitly: a derived object its base, a pointer by the
		// pointer rules (docs/language/04 4.3), `null` a funcptr.
		bool Converts(LoweringContext& ctx, const Value& branch, const Type* from, const Type* target)
		{
			const auto* object = dynamic_cast<const ObjectType*>(from);
			const auto* base = dynamic_cast<const ObjectType*>(target);
			if (object && base) return ClassHierarchy::DerivesFrom(object, base);
			if (target->GetKind() == TypeKind::FunPtr) return IsVoidPointer(from);
			if (target->GetKind() != TypeKind::Pointer || from->GetKind() != TypeKind::Pointer) return false;
			return CheckPointerConversion(target, ctx.LoadIfNeeded(branch)).Ok();
		}

		// A branch naming memory of `type` that a choice can hold the address of.
		bool Addressable(const Value& branch, const Type* type)
		{
			if (!branch.isLValue) return false;
			const LValue* at = branch.lvalue;
			if (!ReferenceBinding::Unaddressable(at).empty() || at->kind == LValue::Kind::IndexOp || at->ephemeral) return false;
			if (at->kind == LValue::Kind::Global && at->sym && at->sym->IsFoldedConstant()) return false;
			// An object type is the common type of objects of it and of types derived from it only.
			return TypeHandle::UnwrapToValue(branch.type) == type || type->GetKind() == TypeKind::Object;
		}
	}

	QueryResult<const Type*> ConditionalBranches::CommonType(LoweringContext& ctx, const Value& a, const Value& b)
	{
		const Type* t = TypeHandle::UnwrapToValue(a.type);
		const Type* e = TypeHandle::UnwrapToValue(b.type);
		if (t == e) return t;
		if (t->IsNumeric() && e->IsNumeric())
			if (TypeKind kind = Type::UsualArithmeticResultKind(ArithmeticKind(t), ArithmeticKind(e)); kind != TypeKind::Invalid)
				return static_cast<const Type*>(ctx.queries().TypeStore()->Builtin(kind));
		const bool toThen = Converts(ctx, b, e, t);
		const bool toElse = Converts(ctx, a, t, e);
		if (toThen && toElse) return IsVoidPointer(t) ? e : t; // `null` takes the other branch's pointer type
		if (toThen) return t;
		if (toElse) return e;
		return QueryResult<const Type*>::Fail(QueryError::NotAValue,
			"the branches of '?:' have no common type: '" + t->GetName() + "' and '" + e->GetName() + "'");
	}

	QueryResult<const HIRNode*> ConditionalBranches::BranchValue(LoweringContext& ctx, const Value& branch, const Type* type)
	{
		const Type* own = TypeHandle::UnwrapToValue(branch.type);
		if (type->GetKind() == TypeKind::Object || (own != type && !type->IsNumeric())) return InitialValue(ctx, branch, type);
		const HIRNode* loaded = ctx.LoadIfNeeded(branch);
		if (loaded->type == type) return loaded;
		return static_cast<const HIRNode*>(ctx.out->NewNode<ConvertNode>(loaded, loaded->type, type));
	}

	const LValue* ConditionalBranches::ChosenLocation(LoweringContext& ctx, const HIRNode* condition, const Value& a, const Value& b, const Type* type)
	{
		if (!Addressable(a, type) || !Addressable(b, type)) return nullptr;
		const bool readOnly = IsReadOnly(ctx, a.lvalue) || IsReadOnly(ctx, b.lvalue);
		const Type* pointer = ctx.queries().TypeStore()->Pointer(type, readOnly);
		// The branches' addresses live while the choice is used; an address taken of the choice escapes both (LastUseMoves).
		LValue chosen; chosen.kind = LValue::Kind::Deref; chosen.type = type;
		chosen.addr = ctx.out->NewNode<TernaryNode>(pointer, condition, ctx.AddressAs(pointer, a.lvalue, true), ctx.AddressAs(pointer, b.lvalue, true));
		return ctx.NewLValue(chosen);
	}
} // namespace Scripting
