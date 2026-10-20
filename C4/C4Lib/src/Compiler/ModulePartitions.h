#pragma once
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// `module` split into `count` partitions that link back into the same program, each as bitcode so another thread
	/// can read it into a context of its own. Every local symbol becomes a hidden global with a ".local" suffix
	/// (left on `module` too), and only the first partition keeps the file-scope assembly.
	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count);
}
