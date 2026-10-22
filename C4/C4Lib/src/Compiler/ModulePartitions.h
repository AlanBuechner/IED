#pragma once
#include <memory>
#include <string>
#include <vector>

namespace llvm
{
	class GlobalValue;
	class Module;
}

namespace Scripting
{
	/// When a split module is optimized: whole, before the split, or each partition alone after it (an incremental
	/// build, D-76).
	enum class PartitionOptimization { Before, After };

	/// `module` split into `count` partitions that link back into the same program, each as bitcode so another thread
	/// can read it into a context of its own. A definition goes where AssignDefinitions places it (by its source file,
	/// or its name), and a partition holds its definitions and declarations of only what they reach, so its bitcode
	/// changes only when its own code does (roadmap 5.3). Every local symbol becomes a hidden global with a ".local"
	/// suffix (left on `module` too). Only the first partition keeps the file-scope assembly and the debug info's
	/// program-wide lists (enums, retained types, imports): a partition's compile unit lists only the globals it
	/// defines. Partitions optimized after the split also keep every definition another one may use (linkonce becomes
	/// weak, also on `module`) and carry copies of the small functions they call (PartitionImports). The partitions are
	/// built on up to `threads` threads, each from a copy of the module read from its bitcode into a context of its own,
	/// so their bitcode is the same whatever the count.
	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count,
		PartitionOptimization optimization = PartitionOptimization::Before, unsigned threads = 1);

	/// A module, in `module`'s context, of `definitions` (in that order) and declarations of only what they reach, built
	/// as a partition is: for a part of a program that leaves it (Script/InlineBodies).
	std::unique_ptr<llvm::Module> ExtractDefinitions(const llvm::Module& module, const std::vector<const llvm::GlobalValue*>& definitions);

	std::string ModuleBitcode(const llvm::Module& module);
}
