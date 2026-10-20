#pragma once
#include "Compiler/CpuTarget.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// How a finished module becomes object files inside the compiler (roadmap 5.1): LLVM's default optimization
	/// pipeline at the `-O` level, as `clang -O<n>` runs it, then the X86 backend writes COFF objects.
	struct ObjectSettings
	{
		int optLevel = 0;
		const CpuTarget* target = nullptr;
		unsigned threads = 1; // how many partitions are assembled at once
	};

	/// An error the backend reports. `cookie` is the inline-asm location cookie (AsmEmitter::LocationCookie) of the
	/// asm block it is about, or 0.
	struct ObjectDiagnostic
	{
		uint64_t cookie = 0;
		std::string message;
	};

	uint64_t InstructionCount(const llvm::Module& module);

	/// How many objects `module` is worth assembling in parallel: one per few thousand instructions (fewer at -O0),
	/// at least one and at most `settings.threads`.
	unsigned PartitionCount(const llvm::Module& module, const ObjectSettings& settings);

	/// Optimizes `module` for the target at `settings.optLevel`. False, with what went wrong in `diagnostics`, when
	/// the target has no code generator or the optimizer reports an error.
	bool OptimizeModule(llvm::Module& module, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics);

	/// Writes `module` as one object per path: with several paths it is split into that many partitions
	/// (ModulePartitions) and assembled on up to `settings.threads` threads. False, with diagnostics, when the
	/// assembler fails.
	bool WriteObjects(llvm::Module& module, const ObjectSettings& settings, const std::vector<std::string>& objectPaths,
		std::vector<ObjectDiagnostic>& diagnostics);
}
