#pragma once
#include "Compiler/CpuTarget.h"
#include "Compiler/ModulePartitions.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llvm { class LLVMContext; class Module; }

namespace C4Compiler { class ObjectCache; }

namespace C4Compiler
{
	/// How a finished module becomes object files inside the compiler (roadmap 5.1): LLVM's default optimization
	/// pipeline at the `-O` level, as `clang -O<n>` runs it, then the X86 backend writes COFF objects.
	struct ObjectSettings
	{
		int optLevel = 0;
		const CpuTarget* target = nullptr;
		unsigned threads = 1; // how many partitions are assembled at once
		bool optimizePartitions = false; // incremental (D-76): each partition is optimized alone, as it is assembled
		bool verify = false; // each partition assembled is verified first (ProgramBackend::VerifyLater)
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

	/// An object a build found in the cache instead of assembling it, with what it is made of: the cache names an object by
	/// what it is made from, so a damaged one (or another compiler's under this one's name) is taken for good until it is
	/// assembled again (BUG-508).
	struct KeptObject
	{
		std::string path;
		size_t partition = 0;       // a partition's: the index of its bitcode among the caller's (WriteCachedObjects' `written`)
		std::string bitcode;        // a whole-program build's partition: its bitcode, which the caller does not keep
		KeyedModule::Builder build; // a keyed module's (the reflection image, the script table)
	};

	struct CachedObjects
	{
		std::vector<std::string> paths; // each once: partitions with the same code share their object
		std::vector<std::string> byPartition; // a program emitted in partitions: each partition's object
		std::vector<KeptObject> kept;   // each object found in the cache, once (not a build server's own: it has no bitcode here)
		size_t reused = 0;
		double splitSeconds = 0;       // splitting the module into partitions and writing each one's bitcode
		struct Assembled { uint64_t instructions = 0; double seconds = 0; };
		std::vector<Assembled> assembled;   // each partition assembled (not reused): its size and how long it took
	};

	/// An optimized module (a whole-program build's) as `partitions` objects kept in `cache` (SplitModuleToBitcode splits
	/// it for code generation): a partition whose code is unchanged since a build that kept it reuses its object, the
	/// others are assembled on up to `settings.threads` threads. No paths, with diagnostics, when the assembler fails.
	CachedObjects WriteCachedObjects(llvm::Module& module, const ObjectSettings& settings, unsigned partitions,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics);

	/// The objects of a program emitted in partitions (D-81), from each partition's bitcode (PartitionBitcode), as
	/// above: an unchanged partition reuses its object. The `keyed` modules' objects follow, each built and assembled
	/// only when missing, before the partitions that are.
	CachedObjects WriteCachedObjects(std::vector<std::string> partitions, std::vector<KeyedModule> keyed, const ObjectSettings& settings,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics);

	/// The same with partition i's bitcode made by `bitcode(i)` on the threads that assemble: a partition whose object
	/// is missing is assembled on the thread that made its bitcode, at once, so an edit's partitions start while the
	/// others' bitcode is still being written. The keyed modules go first, their building the longest work when it is
	/// needed. `written` gets each partition's bitcode. A partition whose `reused` entry is not empty is that object, which
	/// a build server kept for a partition it did not emit again (docs/compiler/07): no bitcode is made for it.
	CachedObjects WriteCachedObjects(size_t partitions, const std::function<std::string(size_t)>& bitcode, std::vector<KeyedModule> keyed,
		const ObjectSettings& settings, const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics, std::vector<std::string>& written,
		const std::vector<std::string>& reused = {});

	/// After a link that failed with objects found in the cache (`kept`): each is moved into the folder `aside` and assembled
	/// again in its place from what it is made of - a partition's bitcode from `bitcode(partition)` unless it carries its
	/// own. One that comes out the same is removed from `aside`; one that differs stays there, and its name is returned:
	/// the cache had kept an object its code does not assemble to. False, with diagnostics, when one cannot be assembled
	/// or moved.
	bool AssembleKeptAgain(const std::vector<KeptObject>& kept, const std::function<std::string(size_t)>& bitcode, const ObjectSettings& settings,
		const std::filesystem::path& aside, std::vector<std::string>& differed, std::vector<ObjectDiagnostic>& diagnostics);
}
