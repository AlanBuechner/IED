#pragma once
#include "Core/Types.h"

namespace C4Compiler::CountedArray
{
	/// The bytes before the first element of a heap array whose elements need destruction: its element count (an int64),
	/// padded to the element's alignment, which the array's finalizer reads (docs/compiler/04).
	uint64 PrefixBytes(uint64 elementAlignment);
} // namespace C4Compiler::CountedArray
