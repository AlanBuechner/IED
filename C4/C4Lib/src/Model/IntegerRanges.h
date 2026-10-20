#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	// The values an integer type (bool, char or integer of any width) holds.
	namespace IntegerRanges
	{
		bool IsUnsigned(const Type* integer);
		uint64 MaxOf(const Type* integer);
		/// A negative `value` fits a signed `integer` wide enough for it.
		bool FitsNegative(int64 value, const Type* integer);
		/// `value` as written fits `integer`; a 64-bit integer takes every bit pattern (`0xFFFFFFFFFFFFFFFF` is a
		/// uint64).
		bool Fits(int64 value, const Type* integer);
	}
} // namespace Scripting
