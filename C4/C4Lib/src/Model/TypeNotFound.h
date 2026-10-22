#pragma once
#include "Core/Core.h"

namespace Scripting
{
	/// "type not found: <name>", with the C4 form when the name is a C++ one (BUG-361): `auto`, `std` and the standard
	/// library's string, vector and map.
	Core::String TypeNotFound(const Core::String& name);
} // namespace Scripting
