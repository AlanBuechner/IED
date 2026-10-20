#pragma once
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// When a split module is optimized: whole, before the split, or each partition alone after it (an incremental
	/// build, D-76).
	enum class PartitionOptimization { Before, After };

	/// `module` split into `count` partitions that link back into the same program, each as bitcode so another thread
	/// can read it into a context of its own. A definition goes where its name hashes to (private data goes with its
	/// first-named user), and a partition holds its definitions and declarations of only what they reach, so its
	/// bitcode changes only when its own code does (roadmap 5.3). Every local symbol becomes a hidden global with a
	/// ".local" suffix (left on `module` too), and only the first partition keeps the file-scope assembly. Partitions
	/// optimized after the split also keep every definition another one may use (linkonce becomes weak, also on
	/// `module`) and carry copies of the small functions they call (PartitionImports).
	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count,
		PartitionOptimization optimization = PartitionOptimization::Before);

	std::string ModuleBitcode(const llvm::Module& module);
}
