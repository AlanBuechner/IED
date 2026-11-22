#pragma once
#include "Model/Types/SignatureType.h"

namespace C4Compiler
{
	/// A function-pointer type: `funcptr<R(a, b)>`. Size is pointer-sized (it is an address).
	class FunPtrType : public SignatureType
	{
	public:
		FunPtrType(const Type* returnType, const Core::Vector<const Type*>& paramTypes);
	};
} // namespace C4Compiler
