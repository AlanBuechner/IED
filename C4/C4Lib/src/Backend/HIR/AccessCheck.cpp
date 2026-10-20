#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"

namespace Scripting::AccessCheck
{
	namespace
	{
		bool IsRestricted(AccessModifier access)
		{
			return access == AccessModifier::Private || access == AccessModifier::Protected;
		}

		bool Grants(const ObjectType* asking, const Member& member)
		{
			if (asking == member.owner) return true;
			return member.access == AccessModifier::Protected && ClassHierarchy::DerivesFrom(asking, member.owner);
		}

		// The function being lowered is member code of every class scope it sits in: its own type and the types
		// that type is nested in. Free functions, extension methods and their lambdas sit in no class scope.
		bool Allowed(const LoweringContext& ctx, const Member& member)
		{
			if (!IsRestricted(member.access)) return true;
			for (const Scope* s = ctx.scope; s; s = s->GetParent())
				if (s->GetKind() == Scope::Kind::Class && s->objectType && Grants(s->objectType, member)) return true;
			return false;
		}

		const char* AccessName(AccessModifier access)
		{
			return access == AccessModifier::Private ? "private" : "protected";
		}

		const char* KindName(MemberKind kind)
		{
			switch (kind)
			{
			case MemberKind::Field: return "field";
			case MemberKind::Method: return "method";
			case MemberKind::Constructor: return "create";
			}
			return "member";
		}

		const ObjectType* DeclaringType(const FunctionRecord& callee)
		{
			if (callee.isExtensionMethod) return nullptr;
			if (callee.scope && callee.scope->GetKind() == Scope::Kind::Class && callee.scope->objectType) return callee.scope->objectType;
			return callee.containingType;
		}

		bool IsCreate(const FunctionRecord& callee)
		{
			for (const Core::String& op : callee.operatorAnnotations)
				if (op == "create") return true;
			return false;
		}

		// A failure that names no place yet is reported at `where`: the part whose create it is.
		QueryResult<void> At(QueryResult<void> result, const SourceLocation& where)
		{
			if (!result.Ok() && !result.loc.line) result.loc = where;
			return result;
		}

		// A type with nothing to run gets no create, so no create body checks the creates of its parts: they are
		// checked here, from the type's own code, as its create would call them.
		QueryResult<void> CheckPartCreates(LoweringContext& ctx, const ObjectType* obj)
		{
			const Scope* asking = ctx.scope;
			if (const Scope* own = ctx.queries().Model()->TypeScope(obj)) ctx.scope = own;
			QueryResult<void> status = QueryResult<void>::Success();
			if (const ObjectType* base = LifecycleFacts::ClassBase(obj)) status = At(CheckDefaultCreate(ctx, base), obj->GetDeclLocation());
			LifecycleFacts::ForEachField(obj, [&](const ObjectType::FieldInfo& f) {
				if (status.Ok()) status = At(CheckDefaultCreate(ctx, f.m_Type), f.m_DeclaringNode ? f.m_DeclaringNode->startLocation : obj->GetDeclLocation());
			});
			ctx.scope = asking;
			return status;
		}
	}

	QueryResult<void> Check(const LoweringContext& ctx, const Member& member)
	{
		if (Allowed(ctx, member)) return QueryResult<void>::Success();
		return QueryResult<void>::Fail(QueryError::NotVisible, Core::String("Cannot access ") + AccessName(member.access) + " " +
			KindName(member.kind) + " '" + member.name + "' of type '" + member.owner->GetName() + "'");
	}

	QueryResult<void> CheckCall(const LoweringContext& ctx, const FunctionRecord& callee)
	{
		const ObjectType* owner = DeclaringType(callee);
		if (!owner) return QueryResult<void>::Success();
		return Check(ctx, { owner, callee.access, IsCreate(callee) ? MemberKind::Constructor : MemberKind::Method, callee.name });
	}

	QueryResult<void> CheckField(const LoweringContext& ctx, const ObjectType* owner, const ObjectType::FieldInfo& field)
	{
		return Check(ctx, { owner, field.m_Access, MemberKind::Field, field.mName });
	}

	QueryResult<void> CheckDefaultCreate(LoweringContext& ctx, const Type* type)
	{
		const Type* t = TypeHandle::UnwrapToValue(type);
		while (t && t->GetKind() == TypeKind::Array) t = TypeHandle::UnwrapToValue(static_cast<const ArrayType*>(t)->GetElementType());
		if (!t || t->GetKind() != TypeKind::Object) return QueryResult<void>::Success();
		// The type's own default create is checked even when it is empty and so never called.
		const auto* obj = static_cast<const ObjectType*>(t);
		const FunctionRecord* create = LifecycleFacts::Own(ctx.queries(), obj).defaultCreate;
		if (!create)
			if (const ObjectType::FieldInfo* unbound = ReferenceBinding::FirstUnbound(obj, {}))
				return QueryResult<void>::Fail(QueryError::NotAValue, "'" + obj->GetName() + "' cannot be default-constructed: " +
					ReferenceBinding::Unbound(obj, *unbound));
		if (!create) create = Lifecycle(ctx).DefaultCreate(obj);
		if (create)
			if (auto allowed = CheckCall(ctx, *create); !allowed.Ok()) return allowed;
		if (LifecycleFacts::NeedsConstruct(ctx.queries(), obj)) return QueryResult<void>::Success();
		return CheckPartCreates(ctx, obj);
	}
} // namespace Scripting::AccessCheck
