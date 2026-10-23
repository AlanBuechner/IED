#pragma once
#include <vector>

namespace llvm
{
	class Module;
}

namespace Scripting
{
	class WorkerPool;

	/// Marks nounwind every function of the program that cannot unwind, in each module that defines or declares it, and
	/// turns every invoke of one into a call, its exception path dropped (doc 04, Exceptions). A function can unwind
	/// when it calls something that can: a call through a pointer, a declaration no module defines that is not nounwind
	/// (the runtime's are, but for the few that run C4 code), or a function of the program that can unwind; a noexcept
	/// function never does. LLVM's function-attrs infers the same within one module once it optimizes; this sees the
	/// whole program, where a split one's other partitions are only declarations to each, and runs at -O0 too.
	void InferNoUnwind(const std::vector<llvm::Module*>& modules, WorkerPool& pool);
} // namespace Scripting
