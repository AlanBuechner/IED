#include "Backend/HIR/CountedArray.h"

namespace Scripting::CountedArray
{
	namespace
	{
		constexpr uint64 kCountSize = sizeof(int64);
	}

	uint64 PrefixBytes(uint64 elementAlignment)
	{
		const uint64 align = elementAlignment ? elementAlignment : 1;
		return (kCountSize + align - 1) / align * align;
	}
} // namespace Scripting::CountedArray
