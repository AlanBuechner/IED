#pragma once
#include <llvm/ADT/StringRef.h>
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
	/// What a split program's local symbols are named with, as hidden globals another partition can name: no C or C++
	/// symbol contains a '.', so they meet no foreign object's names in the link.
	constexpr const char* kLocalSuffix = ".local";

	/// The name the program gave `value`: a split program's local without kLocalSuffix (what a script host's interface
	/// and its scripts know it by), any other value's own.
	llvm::StringRef ProgramName(const llvm::GlobalValue& value);

	/// An optimized `module` (a whole-program build's, after OptimizeModule) split into `count` partitions for code
	/// generation on several threads, each as bitcode so another thread can read it into a context of its own; the
	/// partitions link back into the same program. A definition goes where AssignDefinitions places it (by its source
	/// file, or its name), and a partition holds its definitions and declarations of only what they reach, so its
	/// bitcode changes only when its own code does (roadmap 5.3). Every local symbol becomes a hidden global with a
	/// ".local" suffix (left on `module` too). Only the first partition keeps the file-scope assembly and the debug
	/// info's program-wide lists (enums, retained types, imports): a partition's compile unit lists only the globals it
	/// defines. The partitions are built on up to `threads` threads, each from a copy of the module read from its
	/// bitcode into a context of its own, so their bitcode is the same whatever the count.
	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count, unsigned threads = 1);

	/// A module, in `module`'s context, of `definitions` (in that order) and declarations of only what they reach, built
	/// as a partition is: for a part of a program that leaves it (Script/InlineBodies).
	std::unique_ptr<llvm::Module> ExtractDefinitions(const llvm::Module& module, const std::vector<const llvm::GlobalValue*>& definitions);

	std::string ModuleBitcode(const llvm::Module& module);

	/// How a program emitted in partitions (D-81) is optimized: each partition alone (an incremental build, D-76), or
	/// the partitions merged into one module first (a whole-program build, PartitionMerge).
	enum class PartitionOptimization { Alone, Whole };

	/// The bitcode of a partition of a program emitted in partitions (D-81), which keeps only the declarations its own
	/// code uses, so it changes only with that code. Optimized alone, its linkonce definitions are weak, since its
	/// optimizer would drop one only another partition uses.
	std::string PartitionBitcode(llvm::Module& partition, PartitionOptimization optimization);
}
