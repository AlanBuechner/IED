#include "Model/Types/InitListType.h"
#include "Core/Core.h"

namespace Scripting
{
	InitListType::InitListType(const Type* elementType)
		: Type(TypeKind::InitList), m_ElementType(elementType)
	{
		// std::initializer_list<T> layout: { const T*, size_type } = pointer + uint64.
		m_Size = sizeof(void*) * 2;
		m_Alignment = sizeof(void*);
	}

	Core::String InitListType::GetName() const
	{
		return Core::StringFormat::Format("__init_list<{0}>", m_ElementType ? m_ElementType->ToString() : "void");
	}

	void InitListType::SetMembers(const Type* dataType, const Type* sizeType)
	{
		m_Data = ObjectType::FieldInfo();
		m_Data.mName = "data"; m_Data.m_Type = dataType; m_Data.m_Offset = 0;
		m_Size_ = ObjectType::FieldInfo();
		m_Size_.mName = "size"; m_Size_.m_Type = sizeType; m_Size_.m_Offset = sizeof(void*);
		m_HasMembers = true;
	}

	const ObjectType::FieldInfo* InitListType::FindMember(const Core::String& name) const
	{
		if (!m_HasMembers) return nullptr;
		if (name == "data") return &m_Data;
		if (name == "size") return &m_Size_;
		return nullptr;
	}
} // namespace Scripting
