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

	TypeStore::TypeStore(const ModelMaking* making) : m_Making(making)
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
		const int32 idx = static_cast<int32>(kind);
		if (idx < 0 || idx >= static_cast<int32>(c_BuiltinSlots)) return nullptr;
		if (PrimitiveType* made = m_Builtins[idx].load(std::memory_order_acquire)) return made;
		const std::unique_lock<std::shared_mutex> writing(m_Interning);
		if (PrimitiveType* made = m_Builtins[idx].load(std::memory_order_acquire)) return made;
		PrimitiveType* made = new PrimitiveType(kind);
		m_Builtins[idx].store(made, std::memory_order_release);
		return made;
	}

	void TypeStore::Watch(Type* type) const
	{
		auto* object = dynamic_cast<ObjectType*>(type);
		if (!object || !m_Making) return;
		object->WatchMaking(m_Making);
		m_Making->Made(object->Shown());
	}

	void TypeStore::ShowMadeSoFar()
	{
		const std::shared_lock<std::shared_mutex> reading(m_Interning);
		for (; m_ShownUpTo < m_Owned.Count(); ++m_ShownUpTo)
			if (const auto* object = dynamic_cast<const ObjectType*>(m_Owned[m_ShownUpTo].get())) object->Shown().store(true, std::memory_order_release);
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
		return Interned<Type>(m_PointerCache, std::make_pair(pointee, constReferent), [&] { return new PointerType(pointee, constReferent); });
	}

	const Type* TypeStore::Reference(const Type* referent, bool constReferent)
	{
		if (!referent) return nullptr;
		return Interned<Type>(m_ReferenceCache, std::make_pair(referent, constReferent), [&] { return new ReferenceType(referent, constReferent); });
	}

	const Type* TypeStore::Array(const Type* element, uint64 count)
	{
		if (!element || count == 0) return nullptr;
		return Interned<Type>(m_ArrayCache, std::make_pair(element, count), [&] { return new ArrayType(element, count); });
	}

	const Type* TypeStore::Swizzle(const Type* element, uint32 laneCount)
	{
		if (!element || laneCount == 0) return nullptr;
		return Interned<Type>(m_SwizzleCache, std::make_pair(element, laneCount), [&] {
			SwizzleType* raw = new SwizzleType("packed " + element->GetName() + "[" + std::to_string(laneCount) + "]", element);
			raw->SetRegionPacked(true);
			for (uint32 i = 0; i < laneCount; ++i) raw->AddLaneAt(Core::String(1, (char)('a' + i)), true, i);
			raw->CalculateLayout();
			return raw;
		});
	}

	const Type* TypeStore::InitList(const Type* elementType)
	{
		if (!elementType) return nullptr;
		const Type* data = Pointer(elementType);
		const Type* size = Builtin(TypeKind::UInt64);
		return Interned<Type>(m_InitListCache, elementType, [&] {
			InitListType* t = new InitListType(elementType);
			t->SetMembers(data, size); // data = T* @0, size = uint64 @8
			return t;
		});
	}

	const Type* TypeStore::BracedList(uint32 entries)
	{
		return Interned<Type>(m_BracedListCache, entries, [&] { return new BracedListType(entries); });
	}

	const Type* TypeStore::FunPtr(const Type* returnType, const Core::Vector<const Type*>& paramTypes)
	{
		if (!returnType) return nullptr;
		// By the return type itself, as a type is equal only to itself: its name alone is a template's for every instantiation (BUG-467).
		return Interned<Type>(m_FunPtrCache, KeyOf(returnType, paramTypes, false), [&] { return new FunPtrType(returnType, paramTypes); });
	}

	const Type* TypeStore::Func(const Type* returnType, const Core::Vector<const Type*>& paramTypes)
	{
		if (!returnType) return nullptr;
		return Interned<Type>(m_FuncCache, KeyOf(returnType, paramTypes, false), [&] { return new FuncType(returnType, paramTypes); });
	}

	const FunctionType* TypeStore::Function(const Type* returnType, const Core::Vector<const Type*>& paramTypes, bool variadic)
	{
		return Interned<FunctionType>(m_FunctionCache, KeyOf(returnType, paramTypes, variadic), [&] {
			Core::Vector<FunctionType::ParameterInfo> infos;
			for (const Type* p : paramTypes) { FunctionType::ParameterInfo pi; pi.m_Type = p; infos.Push(pi); }
			return new FunctionType(returnType, infos, variadic);
		});
	}

	void TypeStore::FinalizeLayouts()
	{
		// Fixed point: walk every object and union type and recompute layout until nothing changes. Forward
		// references resolve because every shell exists before this runs (doc 02 pass 2). A union is laid out as its
		// fields resolve, when a struct member may not be laid out yet (BUG-432): it settles here with the objects. A
		// type whose size depends on itself without an indirection never settles - cap the iterations.
		for (int32 pass = 0; pass < 8; ++pass)
		{
			bool changed = false;
			for (auto& owned : m_Owned)
			{
				if (!owned || !owned->IsValid()) continue;
				const size_t beforeSize = owned->GetSize();
				const size_t beforeAlign = owned->GetAlignment();
				if (auto* obj = dynamic_cast<ObjectType*>(owned.get())) obj->CalculateLayout();
				else if (auto* un = dynamic_cast<UnionType*>(owned.get()); un && !un->HasPendingFields()) un->CalculateLayout();
				else continue;
				if (owned->GetSize() != beforeSize || owned->GetAlignment() != beforeAlign)
					changed = true;
			}
			if (!changed) break;
		}
	}

} // namespace Scripting
