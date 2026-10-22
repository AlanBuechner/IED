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

	/// When a split module is optimized: whole, before the split, or each partition alone after it (an incremental
	/// build, D-76), at -O0 only by the always-inliner, so a partition copies only the `inline` functions it calls.
	enum class PartitionOptimization { Before, After, AfterAtO0 };

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

	/// Modules of other definitions of a module, built on the split's threads from the module as it was before the
	/// split (names and linkage as the program gave them): each group of definitions with declarations of what it
	/// reaches (ExtractDefinitions), made bitcode by `finish`.
	struct Extracts
	{
		std::vector<std::vector<const llvm::GlobalValue*>> groups;
		std::string (*finish)(llvm::Module&) = nullptr;
	};

	/// The partitions of SplitModuleToBitcode, and the bitcode of each group of `extracts`, in order.
	struct SplitBitcode
	{
		std::vector<std::string> partitions;
		std::vector<std::string> extracted;
	};

	/// SplitModuleToBitcode, building `extracts` on the same threads, first.
	SplitBitcode SplitModuleAndExtract(llvm::Module& module, unsigned count, PartitionOptimization optimization, unsigned threads,
		const Extracts& extracts);

	/// A module, in `module`'s context, of `definitions` (in that order) and declarations of only what they reach, built
	/// as a partition is: for a part of a program that leaves it (Script/InlineBodies).
	std::unique_ptr<llvm::Module> ExtractDefinitions(const llvm::Module& module, const std::vector<const llvm::GlobalValue*>& definitions);

	std::string ModuleBitcode(const llvm::Module& module);

	/// The bitcode of a partition of a program emitted in partitions (D-81), which keeps only the declarations its own
	/// code uses, so it changes only with that code. Optimized alone after it, its linkonce definitions are weak, since
	/// its optimizer would drop one only another partition uses.
	std::string PartitionBitcode(llvm::Module& partition, PartitionOptimization optimization);
}
