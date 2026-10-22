#pragma once
#include "Core/Core.h"
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// How many calls deep a script takes the host's inline bodies: the functions it calls, then the ones those call.
	/// Each body taken is work for the optimizer, and one further down is rarely inlined into the script.
	constexpr uint32 kInlineImportDepth = 2;

	/// Links into `script` the bodies of `chunks` (the interface's inline bodies, Script/InlineBodies) it reaches within
	/// kInlineImportDepth calls, each `available_externally`: kept only for inlining, every call not inlined goes to the
	/// host. A body may call one in another chunk. Only those bodies are read from the bitcode. False, with the reason
	/// in `error`, when a chunk does not read or link.
	bool ImportInlineBodies(llvm::Module& script, const std::vector<std::string>& chunks, std::string& error);
} // namespace Scripting
