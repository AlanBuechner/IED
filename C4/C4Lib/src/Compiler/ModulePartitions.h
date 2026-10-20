#pragma once
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// `module` split into `count` partitions that link back into the same program, each as bitcode so another thread
	/// can read it into a context of its own. A definition goes where its name hashes to (private data goes with its
	/// first-named user), and a partition holds its definitions and declarations of only what they reach, so its
	/// bitcode changes only when its own code does (roadmap 5.3). Every local symbol becomes a hidden global with a
	/// ".local" suffix (left on `module` too), and only the first partition keeps the file-scope assembly.
	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count);

	std::string ModuleBitcode(const llvm::Module& module);
}
