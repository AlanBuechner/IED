#pragma once
#include "Core/Core.h"

namespace Scripting::CompilerVersion
{
	constexpr uint32 Major = 0;
	constexpr uint32 Minor = 1;
	constexpr uint32 Patch = 0;
	/// The value of the predefined macro C4_VERSION: `major * 10000 + minor * 100 + patch`.
	constexpr uint32 Encoded = Major * 10000 + Minor * 100 + Patch;
}
