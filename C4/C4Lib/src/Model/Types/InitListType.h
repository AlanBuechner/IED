#pragma once
#include "Model/Types/Type.h"
#include "Model/Types/ObjectType.h" // FieldInfo (the list exposes its two aggregate fields)

namespace Scripting
{
	/// C++-style initializer list: __init_list<T>. Mirrors std::initializer_list<T>'s ABI -
	/// a 16-byte aggregate { const T* data, size_type size } passed by value. The element type
	/// T is the only parameterized part; the layout is fixed (pointer + uint64) for all T.
	class InitListType : public Type
	{
	public:
		explicit InitListType(const Type* elementType);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }

		const Type* GetElementType() const { return m_ElementType; }

		// The list IS a 2-field aggregate. Expose those fields so member queries resolve them like any
		// object field: `data` = T* @0, `size` = uint64 @8. Set once by the TypeStore (which owns the
		// pointer/uint64 types) after construction.
		void SetMembers(const Type* dataType, const Type* sizeType);
		const ObjectType::FieldInfo* FindMember(const Core::String& name) const;

	private:
		const Type* m_ElementType = nullptr;
		ObjectType::FieldInfo m_Data;    // T* @0
		ObjectType::FieldInfo m_Size_;   // uint64 @8
		bool m_HasMembers = false;
	};
} // namespace Scripting
