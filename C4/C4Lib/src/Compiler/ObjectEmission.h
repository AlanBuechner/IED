#pragma once
#include "Compiler/CpuTarget.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm { class Module; }

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
	struct CachedObjects
	{
		std::vector<std::string> paths;
		size_t reused = 0;
	};

	/// The module as `partitions` objects kept in `cache` (ModulePartitions splits it): a partition whose code is
	/// unchanged since a build that kept it reuses its object, the others are assembled (and with
	/// `settings.optimizePartitions`, optimized first) on up to `settings.threads` threads. No paths, with diagnostics,
	/// when the optimizer or the assembler fails.
	CachedObjects WriteCachedObjects(llvm::Module& module, const ObjectSettings& settings, unsigned partitions,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics);
}
