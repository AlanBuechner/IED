#pragma once
#include "Compiler/CpuTarget.h"
#include "Compiler/ModulePartitions.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llvm { class LLVMContext; class Module; }

namespace Scripting { class ObjectCache; }

namespace Scripting
{
	/// How a finished module becomes object files inside the compiler (roadmap 5.1): LLVM's default optimization
	/// pipeline at the `-O` level, as `clang -O<n>` runs it, then the X86 backend writes COFF objects.
	struct ObjectSettings
	{
		int optLevel = 0;
		const CpuTarget* target = nullptr;
		unsigned threads = 1; // how many partitions are assembled at once
		bool optimizePartitions = false; // incremental (D-76): each partition is optimized alone, after the split
		bool verify = false; // each partition assembled is verified first (CodeGenDriver::VerifyLater)
	};

	/// An error the backend reports. `cookie` is the inline-asm location cookie (AsmEmitter::LocationCookie) of the
	/// asm block it is about, or 0.
	struct ObjectDiagnostic
	{
		uint64_t cookie = 0;
		std::string message;
	};

	uint64_t InstructionCount(const llvm::Module& module);

	/// How many partitions `module` is worth: about one per few thousand instructions (fewer at -O0), rounded down to
	/// a power of two (so the count, and with it which partition a function is in, changes only when the program
	/// doubles or halves), from 1 to 32.
	unsigned PartitionCount(const llvm::Module& module, const ObjectSettings& settings);

	/// Optimizes `module` for the target at `settings.optLevel`. False, with what went wrong in `diagnostics`, when
	/// the target has no code generator or the optimizer reports an error.
	bool OptimizeModule(llvm::Module& module, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics);

	/// The whole module as one object at `objPath` (`-c`).
	bool WriteObject(llvm::Module& module, const ObjectSettings& settings, const std::string& objPath, std::vector<ObjectDiagnostic>& diagnostics);

	/// The objects of a module, in partition order, and how many of them an earlier build had already assembled.
	/// A module the cache keeps by a key that says everything it is made of, so it is made only when no object of the
	/// key is kept: built by `build` into a context of the thread that assembles it.
	struct KeyedModule
	{
		using Builder = std::function<std::unique_ptr<llvm::Module>(llvm::LLVMContext&)>;
		std::string key;
		Builder build;
	};

	struct CachedObjects
	{
		std::vector<std::string> paths; // each once: partitions with the same code share their object
		size_t reused = 0;
		double splitSeconds = 0;       // splitting the module into partitions and writing each one's bitcode
		struct Assembled { uint64_t instructions = 0; double seconds = 0; };
		std::vector<Assembled> assembled;   // each partition assembled (not reused): its size and how long it took
		std::vector<std::string> extracted; // the bitcode of each group of the extracts asked for, in order
	};

	/// The module as `partitions` objects kept in `cache` (ModulePartitions splits it): a partition whose code is
	/// unchanged since a build that kept it reuses its object, the others are assembled (and with
	/// `settings.optimizePartitions`, optimized first) on up to `settings.threads` threads. No paths, with diagnostics,
	/// when the optimizer or the assembler fails.
	/// With `extracts`, their groups are built on the split's threads too (SplitModuleAndExtract), or here when the
	/// module is kept whole.
	CachedObjects WriteCachedObjects(llvm::Module& module, const ObjectSettings& settings, unsigned partitions,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics, const Extracts& extracts = {});

	/// The objects of a program emitted in partitions (D-81), from each partition's bitcode (PartitionBitcode), as
	/// above: an unchanged partition reuses its object. The `keyed` modules' objects follow, each built and assembled
	/// only when missing, before the partitions that are.
	CachedObjects WriteCachedObjects(std::vector<std::string> partitions, std::vector<KeyedModule> keyed, const ObjectSettings& settings,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics);
}
