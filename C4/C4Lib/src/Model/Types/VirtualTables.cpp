#include "Model/Types/VirtualTables.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/AliasType.h"

namespace Scripting
{
	namespace VirtualTables
	{
		namespace
		{
			const Type* StripAlias(const Type* t)
			{
				while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
				return t;
			}

			bool InSlots(const std::vector<Slot>& slots, const ObjectType::MethodInfo& m)
			{
				for (const Slot& s : slots)
					if (SameSignature(*s.method, m)) return true;
				return false;
			}

			// A method with the signature of a slot in a NON-primary vtable of `type`'s bases overrides that slot,
			// so it lives in that base's vtable and is not a new slot of the primary vtable.
			bool OverridesSecondarySlot(const ObjectType* type, const ObjectType::MethodInfo& m)
			{
				const auto& bases = type->GetBaseTypes();
				for (uint32 i = 0; i < bases.Count(); ++i)
				{
					if (!bases[i] || !bases[i]->IsPolymorphic()) continue;
					for (const VptrLocation& loc : VptrLocations(bases[i]))
					{
						if (bases[i] == type->GetPrimaryBase() && loc.offset == 0) continue;
						if (InSlots(PrimarySlots(loc.layout), m)) return true;
					}
				}
				return false;
			}

			// The sub-object path from `t` (at tOffset) to the `layout` sub-object at targetOffset.
			bool FindChain(const ObjectType* t, int64 tOffset, int64 targetOffset, const ObjectType* layout,
				std::vector<std::pair<const ObjectType*, int64>>& chain)
			{
				chain.push_back({ t, tOffset });
				if (t == layout && tOffset == targetOffset) return true;
				const auto& bases = t->GetBaseTypes();
				for (uint32 i = 0; i < bases.Count(); ++i)
					if (bases[i] && FindChain(bases[i], tOffset + static_cast<int64>(t->GetBaseOffset(i)), targetOffset, layout, chain))
						return true;
				chain.pop_back();
				return false;
			}
		}

		bool SameSignature(const ObjectType::MethodInfo& a, const ObjectType::MethodInfo& b)
		{
			if (a.mName != b.mName || a.m_IsConst != b.m_IsConst) return false;
			if (a.m_ParameterTypes.Count() != b.m_ParameterTypes.Count()) return false;
			for (uint32 i = 0; i < a.m_ParameterTypes.Count(); ++i)
				if (StripAlias(a.m_ParameterTypes[i]) != StripAlias(b.m_ParameterTypes[i])) return false;
			return true;
		}

		std::vector<Slot> PrimarySlots(const ObjectType* type)
		{
			std::vector<Slot> slots;
			if (!type || !type->IsPolymorphic()) return slots;
			if (const ObjectType* primary = type->GetPrimaryBase()) slots = PrimarySlots(primary);
			for (const auto& m : type->GetMethods())
			{
				if (m.m_IsStatic || InSlots(slots, m)) continue; // an override keeps its base's index
				bool declaredVirtual = m.m_IsVirtual || m.m_IsOverride || type->IsInterface();
				if (!declaredVirtual || OverridesSecondarySlot(type, m)) continue;
				slots.push_back({ type, &m });
			}
			return slots;
		}

		std::vector<VptrLocation> VptrLocations(const ObjectType* type)
		{
			std::vector<VptrLocation> locations;
			if (!type || !type->IsPolymorphic()) return locations;
			locations.push_back({ 0, type });
			const auto& bases = type->GetBaseTypes();
			for (uint32 i = 0; i < bases.Count(); ++i)
			{
				if (!bases[i] || !bases[i]->IsPolymorphic()) continue;
				int64 baseOffset = static_cast<int64>(type->GetBaseOffset(i));
				for (const VptrLocation& loc : VptrLocations(bases[i]))
				{
					if (bases[i] == type->GetPrimaryBase() && loc.offset == 0) continue; // shares this type's vptr
					locations.push_back({ baseOffset + loc.offset, loc.layout });
				}
			}
			return locations;
		}

		Overrider FinalOverrider(const ObjectType* type, const VptrLocation& location, const Slot& slot)
		{
			std::vector<std::pair<const ObjectType*, int64>> chain;
			if (!FindChain(type, 0, location.offset, location.layout, chain)) chain.push_back({ location.layout, location.offset });
			for (const ObjectType* cur = location.layout; cur && cur != slot.introducer; )
			{
				cur = cur->GetPrimaryBase(); // the primary base shares the sub-object's offset
				if (cur) chain.push_back({ cur, location.offset });
			}
			for (const auto& [owner, offset] : chain)
				for (const auto& m : owner->GetMethods())
					if (!m.m_IsStatic && SameSignature(m, *slot.method)) return { owner, &m, offset };
			return { slot.introducer, slot.method, location.offset };
		}

		std::vector<Overrider> Overridden(const ObjectType* type, const ObjectType::MethodInfo& method)
		{
			std::vector<Overrider> found;
			if (method.m_IsStatic) return found;
			ClassHierarchy::ForEachSubobject(type, [&](const ObjectType* sub, int64 offset) {
				if (sub == type) return;
				for (const auto& m : sub->GetMethods())
					if (!m.m_IsStatic && (m.m_IsVirtual || m.m_IsOverride || sub->IsInterface()) && SameSignature(m, method))
						found.push_back({ sub, &m, offset });
			});
			return found;
		}

		QueryResult<void> CheckFinalMethods(const ObjectType* type)
		{
			for (const auto& m : type->GetMethods())
			{
				const SourceLocation at = m.m_DeclaringNode ? m.m_DeclaringNode->startLocation : type->GetDeclLocation();
				const std::vector<Overrider> overridden = Overridden(type, m);
				for (const Overrider& o : overridden)
					if (o.method->m_IsFinal)
						return QueryResult<void>::Fail(QueryError::NotAValue, "'" + m.mName + "' overrides a final method of '" + o.owner->GetName() + "'", at);
				const bool isVirtual = m.m_IsVirtual || m.m_IsOverride || type->IsInterface() || !overridden.empty();
				if (m.m_IsFinal && !isVirtual)
					return QueryResult<void>::Fail(QueryError::NotAValue, "'" + m.mName + "' is final but not virtual: only a virtual method can be final", at);
			}
			return QueryResult<void>::Success();
		}

		Dispatch DispatchFor(const ObjectType* receiver, const ObjectType* owner, const ObjectType::MethodInfo* method)
		{
			Dispatch none;
			if (!receiver || !owner || !method || method->m_IsStatic) return none;
			ClassHierarchy::BaseLocation ownerAt = ClassHierarchy::LocateBase(receiver, owner);
			if (!ownerAt.found) return none;
			int64 ownerEnd = ownerAt.offset + static_cast<int64>(owner->GetSize() ? owner->GetSize() : 1);
			for (const VptrLocation& loc : VptrLocations(receiver))
			{
				if (loc.offset < ownerAt.offset || loc.offset >= ownerEnd) continue; // a vptr inside the owner's sub-object
				std::vector<Slot> slots = PrimarySlots(loc.layout);
				for (uint32 i = 0; i < slots.size(); ++i)
				{
					bool ownerOverrides = slots[i].introducer == owner || ClassHierarchy::DerivesFrom(owner, slots[i].introducer);
					if (ownerOverrides && SameSignature(*slots[i].method, *method))
						return { true, loc.offset, static_cast<int32>(i) };
				}
			}
			return none;
		}

		bool IsVirtual(const ObjectType* owner, const ObjectType::MethodInfo& method) { return DispatchFor(owner, owner, &method).isVirtual; }
	}
} // namespace Scripting
