#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/TypeHandle.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/ReferenceType.h"
#include <algorithm>

namespace Scripting::ReferenceBinding
{
	namespace
	{
		bool IsCreate(const FunctionRecord* fn)
		{
			if (!fn || fn->isStatic || !fn->containingType) return false;
			for (const Core::String& a : fn->operatorAnnotations)
				if (a == "create") return true;
			return false;
		}

		bool DeclaredBy(const ObjectType* type, const ObjectType::FieldInfo* field)
		{
			for (const ObjectType::FieldInfo& f : type->GetFields())
				if (&f == field) return true;
			return false;
		}
	}

	const ReferenceType* Of(const Type* t)
	{
		while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
		return t && t->GetKind() == TypeKind::Reference ? static_cast<const ReferenceType*>(t) : nullptr;
	}

	Core::String Problem(const ReferenceType* ref, const Type* located)
	{
		// A reference names a location of its referent's type, or a derived object through its base; a conversion
		// would make a temporary, which a reference cannot hold.
		const Type* referent = TypeHandle::UnwrapToValue(ref->GetReferentType());
		if (!located || located == referent || referent->GetKind() == TypeKind::Void) return "";
		const auto* object = dynamic_cast<const ObjectType*>(located);
		const auto* base = dynamic_cast<const ObjectType*>(referent);
		if (object && base && ClassHierarchy::DerivesFrom(object, base)) return "";
		return "cannot bind '" + ref->GetName() + "' to a '" + located->GetName() + "': a reference names a location of its own type";
	}

	Core::String Unaddressable(const LValue* location)
	{
		if (!location) return "";
		if (location->kind == LValue::Kind::Lane || location->kind == LValue::Kind::VectorLaneDyn)
			return "a lane of a packed vector has no address: copy it into a variable first, or name the whole vector";
		if (location->kind == LValue::Kind::Swizzle) return "a swizzle run has no address: copy it into a 'packed' variable first";
		return "";
	}

	QueryResult<const HIRNode*> Address(LoweringContext& ctx, const ReferenceType* ref, const Value& init, const Type* pointer,
		const Core::String& name)
	{
		using Result = QueryResult<const HIRNode*>;
		// BUG-429: binding to a lane took the vector's address, so the reference read its first lane.
		if (init.isLValue)
			if (Core::String why = Unaddressable(init.lvalue); !why.empty()) return Result::Fail(QueryError::NotAValue, "cannot bind '" + name + "': " + why);
		if (!ref->IsConstReferent() && init.isLValue && IsReadOnly(ctx, init.lvalue))
			return Result::Fail(QueryError::NotAValue,
				"cannot bind a non-const reference to a const object: '" + name + "' would allow mutating read-only data");
		if (!init.isLValue)
			return Result::Fail(QueryError::NotAValue,
				"cannot bind a reference to a prvalue: '" + name + "' must initialize from an lvalue (a location), not a temporary");
		if (Core::String why = Problem(ref, TypeHandle::UnwrapToValue(init.type)); !why.empty()) return Result::Fail(QueryError::NotAValue, why);
		return Result(ctx.AddressAs(pointer, init.lvalue));
	}

	const Type* PointerFor(LoweringContext& ctx, const ReferenceType* ref)
	{
		return ctx.queries().TypeStore()->Pointer(TypeHandle::UnwrapToValue(ref->GetReferentType()), ref->IsConstReferent());
	}

	const LValue* Referent(LoweringContext& ctx, const LValue* field)
	{
		const ReferenceType* ref = Of(field->type);
		LValue deref;
		deref.kind = LValue::Kind::Deref;
		deref.type = TypeHandle::UnwrapToValue(ref->GetReferentType());
		deref.readOnly = ref->IsConstReferent();
		deref.addr = ctx.out->NewNode<LoadNode>(PointerFor(ctx, ref), field);
		deref.boundBy = field;
		return ctx.NewLValue(deref);
	}

	bool BindsInCreate(const LoweringContext& ctx, const LValue* target)
	{
		if (target->kind != LValue::Kind::Deref || !target->boundBy || ctx.capturedThis || !IsCreate(ctx.fn)) return false;
		const LValue* field = target->boundBy;
		return field->base && field->base->kind == LValue::Kind::This && DeclaredBy(ctx.fn->containingType, field->field);
	}

	const HIRNode* Bind(LoweringContext& ctx, const LValue* field, const HIRNode* address)
	{
		return ctx.out->NewNode<StoreNode>(field, address);
	}

	const ObjectType::FieldInfo* FirstUnbound(const ObjectType* type, const std::vector<const ObjectType::FieldInfo*>& bound)
	{
		for (const ObjectType::FieldInfo& f : type->GetFields())
		{
			if (f.m_IsStatic || f.m_Initializer || !Of(f.m_Type)) continue;
			if (std::find(bound.begin(), bound.end(), &f) == bound.end()) return &f;
		}
		return nullptr;
	}

	Core::String Unbound(const ObjectType* type, const ObjectType::FieldInfo& field)
	{
		return "the reference field '" + field.mName + "' of '" + type->GetName() + "' is not bound: a reference names a location from "
			"the start, so give it a braced entry, an initializer, or assign it in a create";
	}
} // namespace Scripting::ReferenceBinding
