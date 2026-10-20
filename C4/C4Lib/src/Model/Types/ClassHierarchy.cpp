#include "Model/Types/ClassHierarchy.h"

namespace Scripting
{
	namespace ClassHierarchy
	{
		void ForEachSubobject(const ObjectType* type, const std::function<void(const ObjectType*, int64)>& visit)
		{
			std::function<void(const ObjectType*, int64)> walk = [&](const ObjectType* t, int64 offset) {
				visit(t, offset);
				const auto& bases = t->GetBaseTypes();
				for (uint32 i = 0; i < bases.Count(); ++i)
					if (bases[i] && bases[i]->IsValid())
						walk(bases[i], offset + static_cast<int64>(t->GetBaseOffset(i)));
			};
			if (type) walk(type, 0);
		}

		std::vector<Subobject> Subobjects(const ObjectType* type)
		{
			std::vector<Subobject> list;
			std::function<void(const ObjectType*, int64)> walk = [&](const ObjectType* t, int64 offset) {
				size_t self = list.size();
				list.push_back({ t, offset, 0, false });
				const auto& bases = t->GetBaseTypes();
				for (uint32 i = 0; i < bases.Count(); ++i)
					if (bases[i] && bases[i]->IsValid())
						walk(bases[i], offset + static_cast<int64>(t->GetBaseOffset(i)));
				list[self].containedBases = static_cast<uint32>(list.size() - self - 1);
			};
			if (type) walk(type, 0);
			for (Subobject& a : list)
				for (const Subobject& b : list)
					if (&a != &b && a.type == b.type) a.ambiguous = true;
			return list;
		}

		BaseLocation LocateBase(const ObjectType* type, const ObjectType* target)
		{
			BaseLocation where;
			ForEachSubobject(type, [&](const ObjectType* sub, int64 offset) {
				if (sub != target) return;
				if (where.found) { where.ambiguous = true; return; }
				where.found = true;
				where.offset = offset;
			});
			return where;
		}

		bool DerivesFrom(const ObjectType* type, const ObjectType* target)
		{
			return LocateBase(type, target).found;
		}

		QueryResult<void> CheckCanInherit(const ObjectType* derived, const Type* base)
		{
			const auto* object = dynamic_cast<const ObjectType*>(base);
			const Core::String name = "'" + derived->GetName() + "'";
			if (!object)
				return QueryResult<void>::Fail(QueryError::NotAValue, name + " cannot inherit from '" + base->GetName() + "': a base is a class, struct or interface");
			if (object == derived) return QueryResult<void>::Fail(QueryError::CyclicDependency, name + " cannot inherit from itself");
			if (DerivesFrom(object, derived))
				return QueryResult<void>::Fail(QueryError::CyclicDependency, name + " cannot inherit from '" + object->GetName() + "': '" +
					object->GetName() + "' already inherits from " + name);
			return QueryResult<void>::Success();
		}

		int64 PointerDelta(const ObjectType* from, const ObjectType* to)
		{
			if (!from || !to || from == to) return 0;
			BaseLocation up = LocateBase(from, to);
			if (up.found) return up.offset;
			BaseLocation down = LocateBase(to, from);
			if (down.found) return -down.offset;
			return 0;
		}

		FieldHit FindField(const ObjectType* type, const Core::String& name)
		{
			FieldHit hit;
			if (!type) return hit;
			for (const auto& f : type->GetFields())
				if (f.mName == name) { hit.field = &f; hit.owner = type; return hit; }
			const auto& bases = type->GetBaseTypes();
			for (uint32 i = 0; i < bases.Count(); ++i)
			{
				if (!bases[i] || !bases[i]->IsValid()) continue;
				FieldHit inBase = FindField(bases[i], name);
				if (!inBase.field) continue;
				inBase.ownerOffset += static_cast<int64>(type->GetBaseOffset(i));
				if (!hit.field) { hit = inBase; continue; }
				// The same static member reached twice is one entity; two instance fields are two sub-objects.
				bool sameEntity = hit.field == inBase.field && (hit.field->m_IsStatic || hit.ownerOffset == inBase.ownerOffset);
				if (!sameEntity) hit.ambiguous = true;
			}
			return hit;
		}

		QueryResult<void> CheckBaseList(const ObjectType* type)
		{
			const auto& bases = type->GetBaseTypes();
			for (uint32 i = 0; i < bases.Count(); ++i)
			{
				const ObjectType* base = bases[i];
				if (!base) continue;
				if (base->IsFinal())
					return QueryResult<void>::Fail(QueryError::NotAValue, "'" + type->GetName() + "' cannot derive from '" + base->GetName() + "': it is final");
				if (type->IsInterface() && !base->IsInterface())
					return QueryResult<void>::Fail(QueryError::NotAValue,
						"interface '" + type->GetName() + "' can only derive from interfaces; '" + base->GetName() + "' is not an interface");
				if (base->IsInterface()) continue;
				if (i != 0)
					return QueryResult<void>::Fail(QueryError::NotAValue,
						"'" + type->GetName() + "' can have one base class, written first; '" + base->GetName() + "' must come before the interfaces or be removed");
			}
			for (uint32 i = 0; i < bases.Count(); ++i)
				for (uint32 j = 0; j < bases.Count(); ++j)
				{
					if (i == j || !bases[i] || !bases[j]) continue;
					if (j < i && bases[i] == bases[j])
						return QueryResult<void>::Fail(QueryError::NotAValue,
							"'" + bases[i]->GetName() + "' appears twice in the base list of '" + type->GetName() + "'");
					// A second copy of a base would make every conversion to it ambiguous.
					if (bases[i] != bases[j] && DerivesFrom(bases[j], bases[i]))
						return QueryResult<void>::Fail(QueryError::NotAValue,
							"'" + bases[i]->GetName() + "' is already a base of '" + bases[j]->GetName() + "'; remove it from the base list of '" +
							type->GetName() + "' (override its methods instead)");
				}
			return QueryResult<void>::Success();
		}

		QueryResult<void> CheckInterfaceMembers(const ObjectType* type)
		{
			if (!type->IsInterface()) return QueryResult<void>::Success();
			for (const auto& f : type->GetFields())
				if (!f.m_IsStatic)
					return QueryResult<void>::Fail(QueryError::NotAValue,
						"interface '" + type->GetName() + "' cannot have the instance field '" + f.mName + "'; use a static field or a class");
			return QueryResult<void>::Success();
		}
	}
} // namespace Scripting
