#pragma once
#include "Model/Types.h"
#include <string>

namespace Scripting
{
	/// MSVC decorated type names, the identity the frame handler and `dynamic_cast` compare. C4 and C++
	/// spell the same type the same way, so exceptions and RTTI cross the interop boundary:
	/// `int32` is `.H`, `Ns::Foo*` is `.PEAVFoo@Ns@@`, `void*` is `.PEAX`. Types C++ has no spelling for
	/// (normalized integers, swizzles, function pointers) get a unique name under `__c4`.
	namespace MsvcNames
	{
		/// The name a TypeDescriptor carries. The top-level pointee's const is not part of it: MSVC records
		/// it in the throw / catch attributes instead.
		std::string TypeDescriptorName(const Type* t);

		/// The TypeDescriptor name of a pointer to `pointee` (`.PEAVFoo@@`; `.PEAX` for void).
		std::string PointerTo(const Type* pointee);
	}
} // namespace Scripting
