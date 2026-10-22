#include "Model/Types/NativeMemory.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ArrayType.h"

namespace Scripting::NativeMemory
{
	namespace
	{
		const char* const kOperator = "native_bytes";

		const Type* Unaliased(const Type* t)
		{
			while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
			return t;
		}

		bool ObjectHolds(const ObjectType* type)
		{
			if (OwnOperator(type)) return true;
			for (const ObjectType* base : type->GetBaseTypes())
				if (ObjectHolds(base)) return true;
			for (const ObjectType::FieldInfo& field : type->GetFields())
				if (Counted(field)) return true;
			return false;
		}
	}

	const ObjectType::MethodInfo* OwnOperator(const ObjectType* type)
	{
		for (uint32 i = 0; i < type->GetMethodCount(); ++i)
			if (type->GetMethods()[i].HasOperator(kOperator)) return &type->GetMethods()[i];
		return nullptr;
	}

	bool Holds(const Type* type)
	{
		type = Unaliased(type);
		if (!type) return false;
		if (type->GetKind() == TypeKind::Array)
		{
			const auto* array = static_cast<const ArrayType*>(type);
			return array->GetElementCount() > 0 && Holds(array->GetElementType());
		}
		return type->GetKind() == TypeKind::Object && ObjectHolds(static_cast<const ObjectType*>(type));
	}

	bool Counted(const ObjectType::FieldInfo& field) { return !field.m_IsStatic && !field.m_InUnionRegion && Holds(field.m_Type); }
} // namespace Scripting::NativeMemory
