#pragma once
#include <vector>

namespace llvm
{
	class GlobalValue;
	class Module;
}

namespace Scripting
{
	/// The definitions of each of `count` partitions of `module`, in the module's order. A definition with debug info
	/// goes by the source file it names, so an edit that moves a file's lines changes only that file's partitions (a
	/// large file is spread over a few by name); one without goes by its name. A comdat's members stay together, and
	/// private data goes with the user whose name sorts first, so a literal travels with its function.
	std::vector<std::vector<const llvm::GlobalValue*>> AssignDefinitions(const llvm::Module& module, unsigned count);
}
