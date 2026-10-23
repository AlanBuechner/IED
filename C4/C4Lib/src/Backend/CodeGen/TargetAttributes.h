#pragma once
#include "Compiler/CpuTarget.h"

namespace llvm { class Module; }

namespace Scripting
{
	class ModuleContext;
	class ProgramModel;

	/// Every function the module defines is compiled for `target` (D-62): its `target-cpu` and `target-features`,
	/// as clang gives them, so the optimizer, the inliner and the object writer use the build's processor. Each also
	/// has an unwind table, as clang gives every function on Windows x64: a fault (SEH), a C++ exception, a debugger
	/// or a profiler unwinds through frames that cannot throw themselves (UnwindInference marks those nounwind).
	void ApplyTarget(llvm::Module& module, const CpuTarget& target);

	/// A function annotated `[target("avx2")]` is built for the program's target with those features on as well: the
	/// optimizer uses them in its body, and the inliner copies it into no caller built without them. Runs after
	/// ApplyTarget; the model checked the names. A function is found by its record, whatever its symbol (a split
	/// program's file-static one has a suffix).
	void ApplyFunctionTargets(ModuleContext& mctx, const ProgramModel& model);
} // namespace Scripting
