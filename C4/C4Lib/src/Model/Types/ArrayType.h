#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A fixed-size array type: `T[N]`. Size/alignment derive from the element on demand so a forward
	/// object type (size unknown when the array was created) still yields correct values later.
	class ArrayType : public Type
	{
	public:
		static constexpr TypeKind kTypeKind = TypeKind::Array;
		ArrayType(const Type* elementType, uint64 elementCount);
		Core::String GetName() const override;
		Core::String ToString() const override;
		const Type* GetElementType() const { return m_ElementType; }
		uint64 GetElementCount() const { return m_ElementCount; }

		size_t GetSize() const override
		{
			return m_ElementType ? m_ElementType->GetSize() * m_ElementCount : 0;
		}
		size_t GetAlignment() const override
		{
			return m_ElementType ? m_ElementType->GetAlignment() : 1;
		}

	private:
		const Type* m_ElementType = nullptr;
		uint64 m_ElementCount = 0;
	};
} // namespace Scripting
