#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"

namespace Scripting
{
	const Type* TypeHandle::GetPointedType(const Type* t)
	{
		if (!t) return nullptr;
		switch (t->GetKind())
		{
		case TypeKind::Pointer:   return static_cast<const PointerType*>(t)->GetPointeeType();
		case TypeKind::Reference: return static_cast<const ReferenceType*>(t)->GetReferentType();
		default:                  return t;
		}
	}

	const Type* TypeHandle::DerefToBase(const Type* t)
	{
		while (t && (t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::Reference))
			t = GetPointedType(t);
		return t;
	}

	const Type* TypeHandle::UnwrapToValue(const Type* t)
	{
		while (t) {
			TypeKind k = t->GetKind();
			// References are transparent indirections and aliases are transparent synonyms: both name the
			// value directly. Pointers are NOT unwrapped - a pointer is a first-class value type (int32*
			// is not int32), and deref/delete/pointer-member-access must see the pointer, not its pointee.
			if (k == TypeKind::Reference)
				t = GetPointedType(t);
			else if (k == TypeKind::Alias)
				t = static_cast<const AliasType*>(t)->GetAliasedType();
			else
				break;
		}
		return t;
	}

	bool TypeHandle::HasTruthValue(const Type* t)
	{
		// A pointer's truth value IS "non-null", which belongs to the pointer itself, not its pointee - an
		// object pointee has no truth value, yet `if (ptr)` is a valid null check. A reference names its
		// referent, so `if (ref)` is the referent's truth value.
		t = UnwrapToValue(t);
		if (!t) return false;
		switch (t->GetKind())
		{
		case TypeKind::Bool:
		case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
		case TypeKind::Float32: case TypeKind::Float64:
		case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32: case TypeKind::Enum:
		case TypeKind::Pointer: case TypeKind::FunPtr: case TypeKind::Func:
			return true;
		default:
			return false; // Object, Union, Swizzle, Array, Function, InitList, Void, ... no truth value
		}
	}

	namespace
	{
		template <typename Fields>
		bool AnyFieldMayHoldPointers(const Fields& fields)
		{
			for (const auto& f : fields)
				if (!f.m_IsStatic && TypeHandle::MayHoldPointers(f.m_Type)) return true;
			return false;
		}

		bool ObjectMayHoldPointers(const ObjectType* obj)
		{
			for (const ObjectType* base : obj->GetBaseTypes())
				if (ObjectMayHoldPointers(base)) return true;
			return AnyFieldMayHoldPointers(obj->GetFields());
		}
	}

	bool TypeHandle::MayHoldPointers(const Type* t)
	{
		while (t && t->GetKind() == TypeKind::Alias)
			t = static_cast<const AliasType*>(t)->GetAliasedType();
		if (!t) return true;
		switch (t->GetKind())
		{
		case TypeKind::Void: case TypeKind::Bool:
		case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
		case TypeKind::Float32: case TypeKind::Float64:
		case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32:
		case TypeKind::Enum: case TypeKind::Swizzle: case TypeKind::FunPtr:
			return false; // a function pointer points at code, never into the heap
		case TypeKind::Array:
			return MayHoldPointers(static_cast<const ArrayType*>(t)->GetElementType());
		case TypeKind::Object:
			return ObjectMayHoldPointers(static_cast<const ObjectType*>(t));
		case TypeKind::Union:
			return AnyFieldMayHoldPointers(static_cast<const UnionType*>(t)->GetFields());
		default:
			return true; // pointers, references, func (its environment), and anything not known to be pointer-free
		}
	}

	TypeHandle TypeHandle::Pointee() const
	{
		if (!m_Type) return {};
		switch (m_Type->GetKind())
		{
		case TypeKind::Pointer:
			return static_cast<const PointerType*>(m_Type)->GetPointeeType();
		case TypeKind::Reference:
			return GetPointedType(m_Type);  // strips exactly one layer
		default:
			return *this;
		}
	}

	TypeHandle TypeHandle::DerefAll() const
	{
		TypeHandle h = *this;
		while (h.IsPointerLike())
			h = h.Pointee();
		return h;
	}

	bool TypeHandle::IsPointerLike() const
	{
		if (!m_Type) return false;
		switch (m_Type->GetKind())
		{
		case TypeKind::Pointer:
		case TypeKind::Reference:
			return true;
		default:
			return false;
		}
	}

	TypeHandle TypeHandle::ResolveAlias() const
	{
		TypeHandle h = *this;
		while (h.m_Type && h.m_Type->GetKind() == TypeKind::Alias)
			h = static_cast<const AliasType*>(h.m_Type)->GetAliasedType();
		return h;
	}

	bool TypeHandle::SupportsIndexing() const
	{
		if (!m_Type) return false;
		return IsArray() || IsPointerLike();
	}

	const ObjectType* TypeHandle::AsObject() const
	{
		TypeHandle base = DerefAll().ResolveAlias();
		if (base.m_Type && base.m_Type->GetKind() == TypeKind::Object)
			return static_cast<const ObjectType*>(base.m_Type);
		return nullptr;
	}

	const ObjectType::FieldInfo* TypeHandle::FindField(const Core::String& name) const
	{
		const ObjectType* obj = AsObject();
		return obj ? obj->GetFieldByName(name) : nullptr;
	}

} // namespace Scripting
