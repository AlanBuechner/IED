#pragma once
#include "Target/Process.h"
#include <cstdint>
#include <vector>

namespace C4DAP::Target
{
	/// The entry points of the catch handlers of the function whose code holds `pc`, read from its x64 unwind
	/// information and the MSVC C++ exception tables it points at (__CxxFrameHandler3's FuncInfo, try-block map
	/// and handler arrays). None for a function without them.
	std::vector<uint64_t> CatchHandlersOf(const Process& process, uint64_t pc);
} // namespace C4DAP::Target
