#include "Model/Types/FunPtrType.h"

namespace Scripting
{
	FunPtrType::FunPtrType(const Type* returnType, const Core::Vector<const Type*>& paramTypes)
		: SignatureType(TypeKind::FunPtr, "funcptr", returnType, paramTypes)
	{
		m_Size = sizeof(void*);
		m_Alignment = sizeof(void*);
	}
} // namespace Scripting
