#pragma once
#include "Core/Core.h"
#include <string>

namespace llvm { class Module; }

namespace Scripting
{
	/// How many calls deep a script takes the host's inline bodies: the functions it calls, then the ones those call.
	/// Each body taken is work for the optimizer, and one further down is rarely inlined into the script.
	constexpr uint32 kInlineImportDepth = 2;

	/// Links into `script` the bodies of `bitcode` (InlineBodies) it reaches within kInlineImportDepth calls, each
	/// `available_externally`: kept only for inlining, every call not inlined goes to the host. Only those bodies are
	/// read from the bitcode. False, with the reason in `error`, when the bitcode does not read or link.
	bool ImportInlineBodies(llvm::Module& script, const std::string& bitcode, std::string& error);
} // namespace Scripting
