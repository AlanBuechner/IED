#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/PointerConversion.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/Callables.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/SignatureType.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/AliasType.h"

namespace Scripting
{
	namespace
	{
		bool IsAggregateValue(const Type* t)
		{
			t = TypeHandle::UnwrapToValue(t);
			return t && (t->GetKind() == TypeKind::Object || t->GetKind() == TypeKind::Array);
		}

	}

	namespace
	{
		using Result = QueryResult<const HIRNode*>;

		// An object or array starts as a relocation of a fresh one, a copy of an existing one, or - for a derived
		// object - a copy of its base sub-object.
		Result ObjectStart(LoweringContext& ctx, const Value& source, const HIRNode* loaded, const Type* value)
		{
			const Type* from = TypeHandle::UnwrapToValue(loaded->type);
			if (Core::String why = ImplicitConversion::ObjectProblem(ctx, from, value); !why.empty())
				return Result::Fail(QueryError::NotAValue, why);
			if (from != value && from->GetKind() != TypeKind::Object) return Result(loaded); // a swizzle run as its packed vector
			if (from == value && Lifecycle::IsFresh(loaded)) return Result(loaded); // relocated
			const LValue* whole = source.isLValue ? source.lvalue
				: loaded->kind == HIRNode::Kind::Load ? static_cast<const LoadNode*>(loaded)->loc : nullptr;
			if (!whole && from == value) return Result(loaded);
			if (!whole) whole = Lifecycle(ctx).AddressableObject(Value::R(loaded, from), from);
			const auto* base = static_cast<const ObjectType*>(value);
			const LValue* part = from == value ? whole
				: SubobjectLValue(ctx, whole, base, ClassHierarchy::LocateBase(static_cast<const ObjectType*>(from), base).offset);
			return Lifecycle(ctx).CopyOf(part);
		}
	}

	QueryResult<const HIRNode*> ConvertedValue(LoweringContext& ctx, const Value& source, const Type* target)
	{
		const Type* value = TypeHandle::UnwrapToValue(target);
		if (const auto* signature = dynamic_cast<const SignatureType*>(value))
		{
			auto callable = ConvertCallable(ctx, source, signature);
			if (!callable.Ok()) return Result::Fail(callable.error, callable.message);
			if (callable.value.converted) return Result(callable.value.converted);
		}
		const HIRNode* loaded = ctx.LoadIfNeeded(source);
		if (auto converts = CheckPointerConversion(value, loaded); !converts.Ok()) return Result::Fail(converts.error, converts.message);
		if (IsAggregateValue(value)) return ObjectStart(ctx, source, loaded, value);
		return ImplicitConversion::Scalar(ctx, loaded, value);
	}

	QueryResult<const HIRNode*> InitialValue(LoweringContext& ctx, const Value& source, const Type* target)
	{
		auto converted = ConvertedValue(ctx, source, target);
		if (converted.Ok() || converted.error == QueryError::Ambiguous || dynamic_cast<const SignatureType*>(TypeHandle::UnwrapToValue(target)))
			return converted;
		const Type* from = source.type ? TypeHandle::UnwrapToValue(source.type) : nullptr;
		return Result::Fail(converted.error, "cannot initialize '" + TypeHandle::UnwrapToValue(target)->GetName() + "' from '" +
			(from ? from->GetName() : Core::String("?")) + "': " + converted.message);
	}

	QueryResult<const HIRNode*> BindArgument(LoweringContext& ctx, const Value& arg, const HIRNode* loaded, const Type* param)
	{
		if (!param || !loaded) return QueryResult<const HIRNode*>(loaded);
		const Type* declared = param;
		while (declared && declared->GetKind() == TypeKind::Alias) declared = static_cast<const AliasType*>(declared)->GetAliasedType();
		if (declared->GetKind() == TypeKind::Reference)
		{
			const auto* ref = static_cast<const ReferenceType*>(declared);
			const Type* referent = TypeHandle::UnwrapToValue(ref->GetReferentType());
			const Type* pointer = ctx.queries().TypeStore()->Pointer(referent, ref->IsConstReferent());
			if (arg.isLValue && !ref->IsConstReferent())
				if (Core::String why = ReadOnlyReason(ctx, arg.lvalue); !why.empty())
					return QueryResult<const HIRNode*>::Fail(QueryError::NotAValue,
						"cannot bind a non-const reference to a const object: " + why + " (take 'const " + referent->GetName() + "&')");
			// D-32: a const reference binds anything convertible to its referent, through a temporary of the referent.
			const Type* argType = TypeHandle::UnwrapToValue(arg.type ? arg.type : loaded->type);
			const auto* argObject = dynamic_cast<const ObjectType*>(argType);
			const auto* referentObject = dynamic_cast<const ObjectType*>(referent);
			const bool bindsAsIs = argType == referent || (argObject && referentObject && ClassHierarchy::DerivesFrom(argObject, referentObject));
			if (ref->IsConstReferent() && !bindsAsIs && referent->GetKind() != TypeKind::Void)
			{
				auto converted = InitialValue(ctx, arg.isLValue ? arg : Value::R(loaded, loaded->type), referent);
				if (!converted.Ok()) return converted;
				return QueryResult<const HIRNode*>(ctx.AddressAs(pointer, Lifecycle(ctx).Temporary(converted.value)));
			}
			if (arg.isLValue) return QueryResult<const HIRNode*>(ctx.AddressAs(pointer, arg.lvalue, true)); // for the call only
			if (Lifecycle::IsFresh(loaded) && LifecycleFacts::NeedsDestroy(ctx.queries(), loaded->type))
				return QueryResult<const HIRNode*>(ctx.AddressAs(pointer, Lifecycle(ctx).Temporary(loaded)));
			return QueryResult<const HIRNode*>(loaded); // codegen materializes an rvalue bound to a reference
		}
		// A by-value parameter starts from its argument: a copy or relocation of an object, a callable converted
		// (a lambda to a func), a scalar converted to the parameter type.
		return InitialValue(ctx, arg.isLValue ? arg : Value::R(loaded, loaded->type), declared);
	}
} // namespace Scripting
