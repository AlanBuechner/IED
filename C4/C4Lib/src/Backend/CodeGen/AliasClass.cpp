#include "Backend/CodeGen/AliasClass.h"
#include "Backend/HIR/IndexLValue.h"
#include "Model/TypeHandle.h"
#include "Model/Types.h"
#include "Model/Types/UnionLayout.h"

namespace Scripting
{
	namespace
	{
		const Type* WithoutAliases(const Type* type)
		{
			while (type && type->GetKind() == TypeKind::Alias) type = static_cast<const AliasType*>(type)->GetAliasedType();
			return type;
		}

		// Every integer of one size is one type; the one-byte types read and write any memory.
		AliasClass OfScalar(TypeKind kind)
		{
			switch (kind)
			{
			case TypeKind::Bool: return AliasClass::Bool;
			case TypeKind::Int16: case TypeKind::UInt16: case TypeKind::SNorm16: case TypeKind::UNorm16: case TypeKind::Char16:
				return AliasClass::Int16;
			case TypeKind::Int32: case TypeKind::UInt32: case TypeKind::SNorm32: case TypeKind::UNorm32: case TypeKind::Char32:
				return AliasClass::Int32;
			case TypeKind::Int64: case TypeKind::UInt64: case TypeKind::SNorm64: case TypeKind::UNorm64:
				return AliasClass::Int64;
			case TypeKind::Float32: return AliasClass::Float32;
			case TypeKind::Float64: return AliasClass::Float64;
			case TypeKind::Pointer: case TypeKind::Reference: case TypeKind::FunPtr: case TypeKind::Function: case TypeKind::Func:
				return AliasClass::Pointer;
			default: return AliasClass::Bytes;
			}
		}
	}

	AliasClass Meet(AliasClass a, AliasClass b)
	{
		if (a == AliasClass::None) return b;
		if (b == AliasClass::None) return a;
		return a == b ? a : AliasClass::Bytes;
	}

	AliasClass AliasClassifier::Of(const Type* type)
	{
		const AliasClass parts = PartsOf(type);
		return parts == AliasClass::None ? AliasClass::Bytes : parts;
	}

	AliasClass AliasClassifier::OfAccess(const LValue* loc, const Type* accessed)
	{
		return Meet(Of(accessed), UnionsAlong(loc));
	}

	AliasClass AliasClassifier::PartsOf(const Type* type)
	{
		if (auto known = m_Parts.find(type); known != m_Parts.end()) return known->second;
		const AliasClass parts = Compute(type);
		m_Parts[type] = parts;
		return parts;
	}

	AliasClass AliasClassifier::Compute(const Type* type)
	{
		type = WithoutAliases(type);
		if (!type) return AliasClass::Bytes;
		switch (type->GetKind())
		{
		case TypeKind::Enum:
		{
			const Type* underlying = static_cast<const EnumType*>(type)->GetUnderlyingType();
			return underlying ? PartsOf(underlying) : AliasClass::Int32;
		}
		case TypeKind::Array: return PartsOf(static_cast<const ArrayType*>(type)->GetElementType());
		case TypeKind::Swizzle: return PartsOf(static_cast<const SwizzleType*>(type)->GetElementType());
		case TypeKind::Object: return OfObject(static_cast<const ObjectType*>(type));
		case TypeKind::Union: return OfMembers(UnionLayout::RegionMembers(static_cast<const UnionType*>(type)->GetFields(), -1));
		default: return OfScalar(type->GetKind());
		}
	}

	AliasClass AliasClassifier::OfObject(const ObjectType* object)
	{
		if (object->IsPurePackedVector()) return PartsOf(object->GetPackedVectorSwizzle());
		AliasClass parts = m_HasVptrs(object) ? AliasClass::Pointer : AliasClass::None;
		for (const ObjectType* base : object->GetBaseTypes()) parts = Meet(parts, PartsOf(base));
		for (const ObjectType::FieldInfo& field : object->GetFields())
			if (!field.m_IsStatic) parts = Meet(parts, PartsOf(field.m_Type));
		return parts;
	}

	AliasClass AliasClassifier::OfMembers(const std::vector<const ObjectType::FieldInfo*>& members)
	{
		AliasClass parts = AliasClass::None;
		for (const ObjectType::FieldInfo* member : members) parts = Meet(parts, PartsOf(member->m_Type));
		return parts;
	}

	// The class of the unions an access goes through, None when it goes through none: a member of one, or an element
	// of an array that is one.
	AliasClass AliasClassifier::UnionsAlong(const LValue* loc)
	{
		if (!loc) return AliasClass::None;
		switch (loc->kind)
		{
		case LValue::Kind::Field: return Meet(UnionHolding(loc), UnionsAlong(loc->base));
		case LValue::Kind::Base: case LValue::Kind::Lane: case LValue::Kind::Swizzle: case LValue::Kind::VectorLaneDyn:
			return UnionsAlong(loc->base);
		case LValue::Kind::Deref: return UnionsAlong(IndexedArray(loc));
		default: return AliasClass::None;
		}
	}

	// The class of the union a field lvalue is a member of (a named union, or an anonymous union region of an object),
	// None when it is a plain field.
	AliasClass AliasClassifier::UnionHolding(const LValue* field)
	{
		if (!field->field || !field->base) return AliasClass::None;
		const Type* owner = TypeHandle::UnwrapToValue(field->base->type);
		if (!owner) return AliasClass::None;
		if (owner->GetKind() == TypeKind::Union) return PartsOf(owner);
		if (!field->field->m_InUnionRegion || owner->GetKind() != TypeKind::Object) return AliasClass::None;
		return OfMembers(UnionLayout::RegionMembers(static_cast<const ObjectType*>(owner)->GetFields(), field->field->m_UnionRegionId));
	}
} // namespace Scripting
