#pragma once
#include "Model/Types/SignatureType.h"

namespace Scripting
{
	/// `func<R(a, b)>` (docs/language/07, D-22): any callable of that signature as a 16-byte value - the code to
	/// call and the environment it is called with (a closure's captures, or the function a funcptr names).
	class FuncType : public SignatureType
	{
	public:
		static constexpr size_t kSize = 2 * sizeof(void*);

		FuncType(const Type* returnType, const Core::Vector<const Type*>& paramTypes);
	};
} // namespace Scripting
