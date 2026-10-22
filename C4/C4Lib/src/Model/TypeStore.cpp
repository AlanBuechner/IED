#include "Model/TypeStore.h"

namespace Scripting
{
	namespace
	{
		// The built-in aliases (D-01, D-02): transparent names of a primitive.
		struct BuiltinAlias { const char* name; TypeKind target; };
		constexpr BuiltinAlias kBuiltinAliases[] = {
			{ "int", TypeKind::Int32 },
			{ "uint", TypeKind::UInt32 },
			{ "char", TypeKind::Char8 },
		};
	}

	TypeStore::TypeStore() : m_Builtins(64, nullptr)
	{
		// Seed every primitive singleton and its name so FindBuiltin answers "int32", "float", ...; a built-in
		// alias names the same singleton (D-01, D-02), so `int` and `int32` are one type.
		static const TypeKind kPrimitiveKinds[] = {
			TypeKind::Void, TypeKind::Bool,
			TypeKind::Int8, TypeKind::Int16, TypeKind::Int32, TypeKind::Int64,
			TypeKind::UInt8, TypeKind::UInt16, TypeKind::UInt32, TypeKind::UInt64,
			TypeKind::SNorm8, TypeKind::SNorm16, TypeKind::SNorm32, TypeKind::SNorm64,
			TypeKind::UNorm8, TypeKind::UNorm16, TypeKind::UNorm32, TypeKind::UNorm64,
			TypeKind::Float32, TypeKind::Float64, TypeKind::Char8, TypeKind::Char16, TypeKind::Char32
		};
		for (TypeKind kind : kPrimitiveKinds)
			m_BuiltinByName[Builtin(kind)->GetName()] = Builtin(kind);
		for (const BuiltinAlias& alias : kBuiltinAliases)
			m_BuiltinByName[alias.name] = Builtin(alias.target);
		// D-35: a variadic argument list; on Win64 a cursor into the 8-byte argument slots.
		m_BuiltinByName["va_list"] = Own(new AliasType("va_list", Pointer(Builtin(TypeKind::Char8))));
	}

	const Type* TypeStore::Builtin(TypeKind kind)
	{
		int32 idx = static_cast<int32>(kind);
		if (idx < 0 || idx >= static_cast<int32>(m_Builtins.Count())) return nullptr;
		if (!m_Builtins[idx])
			m_Builtins[idx] = new PrimitiveType(kind);
		return m_Builtins[idx];
	}

	bool TypeStore::IsBuiltinName(const Core::String& name)
	{
		static const TypeStore kNames; // immutable: only its name table is read
		return kNames.FindBuiltin(name) != nullptr;
	}

	const Type* TypeStore::FindBuiltin(const Core::String& name) const
	{
		auto it = m_BuiltinByName.find(name);
		return it != m_BuiltinByName.end() ? it->second : nullptr;
	}

	const Type* TypeStore::Pointer(const Type* pointee, bool constReferent)
	{
		if (!pointee) return nullptr;
		auto key = std::make_pair(pointee, constReferent);
		auto it = m_PointerCache.find(key);
		if (it != m_PointerCache.end()) return it->second;
		const Type* t = Own(new PointerType(pointee, constReferent));
		m_PointerCache.emplace(key, t);
		return t;
	}

	const Type* TypeStore::Reference(const Type* referent, bool constReferent)
	{
		if (!referent) return nullptr;
		auto key = std::make_pair(referent, constReferent);
		auto it = m_ReferenceCache.find(key);
		if (it != m_ReferenceCache.end()) return it->second;
		const Type* t = Own(new ReferenceType(referent, constReferent));
		m_ReferenceCache.emplace(key, t);
		return t;
	}

	const Type* TypeStore::Array(const Type* element, uint64 count)
	{
		if (!element || count == 0) return nullptr;
		auto key = std::make_pair(element, count);
		auto it = m_ArrayCache.find(key);
		if (it != m_ArrayCache.end()) return it->second;
		const Type* t = Own(new ArrayType(element, count));
		m_ArrayCache.emplace(key, t);
		return t;
	}

	const Type* TypeStore::Swizzle(const Type* element, uint32 laneCount)
	{
		if (!element || laneCount == 0) return nullptr;
		auto key = std::make_pair(element, laneCount);
		auto it = m_SwizzleCache.find(key);
		if (it != m_SwizzleCache.end()) return it->second;
		SwizzleType* raw = new SwizzleType("packed " + element->GetName() + "[" + std::to_string(laneCount) + "]", element);
		raw->SetRegionPacked(true);
		for (uint32 i = 0; i < laneCount; ++i) raw->AddLaneAt(Core::String(1, (char)('a' + i)), true, i);
		raw->CalculateLayout();
		const Type* t = Own(raw);
		m_SwizzleCache.emplace(key, t);
		return t;
	}

	const Type* TypeStore::InitList(const Type* elementType)
	{
		if (!elementType) return nullptr;
		auto it = m_InitListCache.find(elementType);
		if (it != m_InitListCache.end()) return it->second;
		InitListType* t = new InitListType(elementType);
		t->SetMembers(Pointer(elementType), Builtin(TypeKind::UInt64)); // data = T* @0, size = uint64 @8
		const Type* owned = Own(t);
		m_InitListCache.emplace(elementType, owned);
		return owned;
	}

	const Type* TypeStore::BracedList(uint32 entries)
	{
		auto it = m_BracedListCache.find(entries);
		if (it != m_BracedListCache.end()) return it->second;
		const Type* owned = Own(new BracedListType(entries));
		m_BracedListCache.emplace(entries, owned);
		return owned;
	}

	const Type* TypeStore::FunPtr(const Type* returnType, const Core::Vector<const Type*>& paramTypes)
	{
		if (!returnType) return nullptr;
		auto [entry, fresh] = m_FunPtrCache.try_emplace(KeyOf(returnType->GetName(), paramTypes, false), nullptr);
		if (fresh) entry->second = Own(new FunPtrType(returnType, paramTypes));
		return entry->second;
	}

	const Type* TypeStore::Func(const Type* returnType, const Core::Vector<const Type*>& paramTypes)
	{
		if (!returnType) return nullptr;
		auto [entry, fresh] = m_FuncCache.try_emplace(KeyOf(returnType->GetName(), paramTypes, false), nullptr);
		if (fresh) entry->second = Own(new FuncType(returnType, paramTypes));
		return entry->second;
	}

	const FunctionType* TypeStore::Function(const Type* returnType, const Core::Vector<const Type*>& paramTypes, bool variadic)
	{
		auto [entry, fresh] = m_FunctionCache.try_emplace(KeyOf(returnType, paramTypes, variadic), nullptr);
		if (!fresh) return entry->second;
		Core::Vector<FunctionType::ParameterInfo> infos;
		for (const Type* p : paramTypes) { FunctionType::ParameterInfo pi; pi.m_Type = p; infos.Push(pi); }
		entry->second = Own(new FunctionType(returnType, infos, variadic));
		return entry->second;
	}

	void TypeStore::FinalizeLayouts()
	{
		// Fixed point: walk every object type and recompute layout until nothing changes. Forward
		// references resolve because every shell exists before this runs (doc 02 pass 2). A type
		// whose size depends on itself without an indirection never settles - cap the iterations.
		for (int32 pass = 0; pass < 8; ++pass)
		{
			bool changed = false;
			for (auto& owned : m_Owned)
			{
				if (!owned || !owned->IsValid()) continue;
				const ObjectType* obj = dynamic_cast<const ObjectType*>(owned.get());
				if (!obj) continue;
				size_t beforeSize = obj->GetSize();
				size_t beforeAlign = obj->GetAlignment();
				const_cast<ObjectType*>(obj)->CalculateLayout();
				if (obj->GetSize() != beforeSize || obj->GetAlignment() != beforeAlign)
					changed = true;
			}
			if (!changed) break;
		}
	}

} // namespace Scripting
