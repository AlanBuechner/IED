#pragma once
#include "Compiler/CpuTarget.h"

namespace llvm { class Module; }

namespace Scripting
{
	class ProgramModel;

	/// Every function the module defines is compiled for `target` (D-62): its `target-cpu` and `target-features`,
	/// as clang gives them, so the optimizer, the inliner and the object writer use the build's processor.
	void ApplyTarget(llvm::Module& module, const CpuTarget& target);

	/// A function annotated `[target("avx2")]` is built for the program's target with those features on as well: the
	/// optimizer uses them in its body, and the inliner copies it into no caller built without them. Runs after
	/// ApplyTarget; the model checked the names.
	void ApplyFunctionTargets(llvm::Module& module, const ProgramModel& model);
} // namespace Scripting
