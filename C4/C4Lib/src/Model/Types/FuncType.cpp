#include "Model/Types/FuncType.h"

namespace C4Compiler
{
	FuncType::FuncType(const Type* returnType, const Core::Vector<const Type*>& paramTypes)
		: SignatureType(TypeKind::Func, "func", returnType, paramTypes)
	{
		m_Size = kSize;
		m_Alignment = sizeof(void*);
	}
} // namespace C4Compiler
