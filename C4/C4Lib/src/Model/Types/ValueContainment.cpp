#include "Model/Types/ValueContainment.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/UnionType.h"
#include <functional>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		using Visit = std::function<bool(const Core::String& step, const Type* held)>;

		// The object or union a field holds by value under its array extents and aliases; null for a pointer,
		// reference, function or number.
		const Type* HeldByValue(const Type* type)
		{
			while (type)
			{
				if (const auto* array = dynamic_cast<const ArrayType*>(type)) type = array->GetElementType();
				else if (const auto* alias = dynamic_cast<const AliasType*>(type)) type = alias->GetAliasedType();
				else break;
			}
			return type && (type->GetKind() == TypeKind::Object || type->GetKind() == TypeKind::Union) ? type : nullptr;
		}

		bool VisitFields(const Type* holder, const Core::Vector<ObjectType::FieldInfo>& fields, const Visit& visit)
		{
			for (const auto& field : fields)
			{
				const Type* held = field.m_IsStatic ? nullptr : HeldByValue(field.m_Type);
				if (held && visit("'" + holder->GetName() + "." + field.mName + "' holds a '" + held->GetName() + "'", held)) return true;
			}
			return false;
		}

		// Stops at the first `visit` that returns true.
		bool VisitHeld(const Type* holder, const Visit& visit)
		{
			if (const auto* obj = dynamic_cast<const ObjectType*>(holder))
			{
				for (const ObjectType* base : obj->GetBaseTypes())
					if (base && visit("'" + obj->GetName() + "' derives from '" + base->GetName() + "'", base)) return true;
				return VisitFields(holder, obj->GetFields(), visit);
			}
			if (const auto* un = dynamic_cast<const UnionType*>(holder)) return VisitFields(holder, un->GetFields(), visit);
			return false;
		}

		// Depth first from `at`: true when `target` is reached, with the steps taken in `path`.
		bool Reaches(const Type* at, const Type* target, std::unordered_set<const Type*>& seen, Core::Vector<Core::String>& path)
		{
			return VisitHeld(at, [&](const Core::String& step, const Type* held)
			{
				path.Push(step);
				if (held == target || (seen.insert(held).second && Reaches(held, target, seen, path))) return true;
				path.Pop();
				return false;
			});
		}
	}

	QueryResult<void> ValueContainment::CheckNotSelfContaining(const ObjectType* type)
	{
		std::unordered_set<const Type*> seen{ type };
		Core::Vector<Core::String> path;
		if (!Reaches(type, type, seen, path)) return QueryResult<void>::Success();
		Core::String steps;
		for (const Core::String& step : path) steps += (steps.empty() ? "" : ", ") + step;
		return QueryResult<void>::Fail(QueryError::CyclicDependency,
			"'" + type->GetName() + "' holds itself by value (" + steps + "), so it has no size: hold one of them by pointer");
	}
} // namespace Scripting
