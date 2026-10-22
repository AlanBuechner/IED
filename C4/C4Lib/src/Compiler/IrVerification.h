#pragma once
#include <optional>
#include <string>

namespace llvm { class Module; }

namespace Scripting
{
	/// What is wrong with `module`'s IR (the first function the verifier rejects, else the module-level complaint), or
	/// nothing when it verifies: invalid IR is a compiler bug, reported before LLVM is handed it.
	std::optional<std::string> InvalidIr(const llvm::Module& module);
}
