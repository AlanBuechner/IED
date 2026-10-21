#pragma once
#include "Script/ScriptJit.h"
#include <string>

namespace Scripting
{
	/// The marker a COFF object names when its code uses floating point: data the linker looks for, never called.
	inline constexpr const char* kFloatingPointMarker = "_fltused";

	/// What the code a compiler emits may call without naming it: the memory helpers, the stack probe, and the math
	/// functions LLVM lowers intrinsics to. Null for any other name.
	void* SupportSymbol(const std::string& name);

	/// What a script names (docs/runtime/04 section 6.1): the host's symbol (`__c4_host_CxxFrameHandler3` is the host's
	/// personality routine), else a support function, else an export of a DLL the process loaded. Null when nothing
	/// defines it.
	void* ResolveScriptSymbol(const HostSymbolTable& host, const std::string& name);
} // namespace Scripting
