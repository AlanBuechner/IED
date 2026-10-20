#pragma once
#include "Compiler/CpuTarget.h"

namespace llvm { class Module; }

namespace Scripting
{
	/// Every function the module defines is compiled for `target` (D-62): its `target-cpu` and `target-features`,
	/// as clang gives them, so the optimizer, the inliner and the object writer use the build's processor.
	void ApplyTarget(llvm::Module& module, const CpuTarget& target);
} // namespace Scripting
