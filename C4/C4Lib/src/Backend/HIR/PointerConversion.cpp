#include "Backend/HIR/PointerConversion.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/PointerType.h"

namespace Scripting
{
	namespace
	{
		bool IsVoid(const Type* t) { return t && t->GetKind() == TypeKind::Void; }

		bool IsNullConstant(const HIRNode* value)
		{
			const auto* constant = dynamic_cast<const ConstantNode*>(value);
			return constant && constant->value.AsInt() == 0;
		}

		bool IsVoidPointer(const Type* t)
		{
			t = t ? TypeHandle::UnwrapToValue(t) : nullptr;
			return t && t->GetKind() == TypeKind::Pointer && IsVoid(static_cast<const PointerType*>(t)->GetPointeeType());
		}

		QueryResult<void> PointeeConverts(const Type* from, const Type* to)
		{
			// `void*` is universal, and so is `void**` against any `T**` (COM out-parameters).
			if (from == to || IsVoid(from) || IsVoid(to) || IsVoidPointer(from) || IsVoidPointer(to)) return QueryResult<void>::Success();
			const auto* derived = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(from));
			const auto* base = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(to));
			if (derived && base && ClassHierarchy::DerivesFrom(base, derived))
				return QueryResult<void>::Fail(QueryError::NotAValue, "a base pointer becomes a derived one only through 'as' (a checked cast)");
			if (!derived || !base || !ClassHierarchy::DerivesFrom(derived, base))
				return QueryResult<void>::Fail(QueryError::NotAValue, "the pointees are unrelated");
			if (ClassHierarchy::LocateBase(derived, base).ambiguous)
				return QueryResult<void>::Fail(QueryError::Ambiguous, "ambiguous conversion: '" + base->GetName() + "' is a base of '" +
					derived->GetName() + "' more than once");
			return QueryResult<void>::Success();
		}
	}

	QueryResult<void> CheckPointerConversion(const Type* target, const HIRNode* value)
	{
		const Type* to = target ? TypeHandle::UnwrapToValue(target) : nullptr;
		const Type* from = value ? value->type : nullptr;
		if (!to || to->GetKind() != TypeKind::Pointer || !from || IsNullConstant(value)) return QueryResult<void>::Success();
		from = TypeHandle::UnwrapToValue(from);
		const auto* toPointer = static_cast<const PointerType*>(to);
		if (from->GetKind() == TypeKind::FunPtr)
			return IsVoid(toPointer->GetPointeeType()) ? QueryResult<void>::Success()
				: QueryResult<void>::Fail(QueryError::NotAValue, "a function pointer converts only to void*");
		if (from->GetKind() != TypeKind::Pointer) return QueryResult<void>::Success();
		const auto* fromPointer = static_cast<const PointerType*>(from);
		if (auto pointees = PointeeConverts(fromPointer->GetPointeeType(), toPointer->GetPointeeType()); !pointees.Ok()) return pointees;
		// `void*` is the universal handle (C interop takes `const char*` through it); every other target keeps const.
		if (fromPointer->IsConstReferent() && !toPointer->IsConstReferent() && !IsVoid(toPointer->GetPointeeType()))
			return QueryResult<void>::Fail(QueryError::NotAValue, "dropping const is not allowed");
		return QueryResult<void>::Success();
	}
} // namespace Scripting
