#pragma once
#include "Core/Core.h"
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// The most instructions a host function may have (lowered, before optimization) for scripts to inline it.
	constexpr uint32 kInlineBodyLimit = 160;

	/// The bitcode of the host functions a script may inline (docs/runtime/04 section 3.3): each function `module`
	/// defines with at most kInlineBodyLimit instructions whose body names only `hostSymbols`, intrinsics and
	/// private constants (copied with it), and no thread_local. What it names is declared under the same name. Empty
	/// when no function qualifies.
	std::string InlineBodies(const llvm::Module& module, const std::vector<std::string>& hostSymbols);
} // namespace Scripting
