#include "Compiler/ObjectEmission.h"
#include "Compiler/AggregateCopies.h"
#include "Compiler/CodeViewLocations.h"
#include "Compiler/IrVerification.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/ObjectCache.h"
#include "Compiler/ParallelFor.h"
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		// About 15 ms of code generation per partition. A partition is also what the object cache keeps, so smaller ones
		// reassemble less after an edit, and the split costs the same whatever their number. An instruction takes
		// about three times as long to assemble from -O1 on.
		constexpr uint64_t kInstructionsPerPartitionO0 = 3000;
		constexpr uint64_t kInstructionsPerPartition = 1000;
		// Partitions are also the unit of the object cache: more of them reassemble less after a small edit.
		constexpr uint64_t kMaxPartitions = 64;

		// LLVM's target registry is process-wide; registering X86 twice is harmless but wasted work. The assembly parser
		// assembles inline asm.
		void InitializeX86()
		{
			static std::once_flag once;
			std::call_once(once, [] {
				LLVMInitializeX86TargetInfo();
				LLVMInitializeX86Target();
				LLVMInitializeX86TargetMC();
				LLVMInitializeX86AsmPrinter();
				LLVMInitializeX86AsmParser();
			});
		}

		// Errors the backend reports while it optimizes and assembles: inline assembly the assembler rejects, above all.
		class Diagnostics : public llvm::DiagnosticHandler
		{
		public:
			explicit Diagnostics(std::vector<ObjectDiagnostic>& errors) : m_Errors(errors) {}

			bool handleDiagnostics(const llvm::DiagnosticInfo& info) override
			{
				if (info.getSeverity() != llvm::DS_Error) return true;
				ObjectDiagnostic diagnostic;
				if (const auto* source = llvm::dyn_cast<llvm::DiagnosticInfoSrcMgr>(&info))
				{
					diagnostic.cookie = source->getLocCookie();
					diagnostic.message = source->getSMDiag().getMessage().str() + ": '" + source->getSMDiag().getLineContents().trim().str() + "'";
				}
				else
				{
					if (const auto* inlineAsm = llvm::dyn_cast<llvm::DiagnosticInfoInlineAsm>(&info)) diagnostic.cookie = inlineAsm->getLocCookie();
					llvm::raw_string_ostream os(diagnostic.message);
					llvm::DiagnosticPrinterRawOStream printer(os);
					info.print(printer);
				}
				m_Errors.push_back(diagnostic);
				return true;
			}

		private:
			std::vector<ObjectDiagnostic>& m_Errors;
		};

		// Collects a context's errors into `errors` while it lives, then gives the context its handler back.
		class CollectedDiagnostics
		{
		public:
			CollectedDiagnostics(llvm::LLVMContext& context, std::vector<ObjectDiagnostic>& errors)
				: m_Context(context), m_Previous(context.getDiagnosticHandler())
			{
				context.setDiagnosticHandler(std::make_unique<Diagnostics>(errors));
			}
			~CollectedDiagnostics() { m_Context.setDiagnosticHandler(std::move(m_Previous)); }

		private:
			llvm::LLVMContext& m_Context;
			std::unique_ptr<llvm::DiagnosticHandler> m_Previous;
		};

		llvm::OptimizationLevel PipelineLevel(int optLevel)
		{
			if (optLevel <= 1) return llvm::OptimizationLevel::O1;
			return optLevel == 2 ? llvm::OptimizationLevel::O2 : llvm::OptimizationLevel::O3;
		}

		llvm::CodeGenOptLevel CodeGenLevel(int optLevel)
		{
			switch (optLevel)
			{
			case 0: return llvm::CodeGenOptLevel::None;
			case 1: return llvm::CodeGenOptLevel::Less;
			case 2: return llvm::CodeGenOptLevel::Default;
			default: return llvm::CodeGenOptLevel::Aggressive;
			}
		}

		// The target machine for `module`, which gets the machine's data layout; nullptr with the reason in `diagnostics`.
		std::unique_ptr<llvm::TargetMachine> MachineFor(llvm::Module& module, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics)
		{
			InitializeX86();
			const llvm::Triple& triple = module.getTargetTriple();
			std::string error;
			const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
			if (!target)
			{
				diagnostics.push_back({ 0, "no code generator for '" + triple.str() + "': " + error });
				return nullptr;
			}
			llvm::TargetOptions options;
			std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(triple, settings.target->Cpu(),
				settings.target->LlvmFeatures(), options, std::nullopt, std::nullopt, CodeGenLevel(settings.optLevel)));
			module.setDataLayout(machine->createDataLayout());
			return machine;
		}

		// The pipeline `clang -O<n>` runs: unrolling and both vectorizers from -O2 on.
		void RunPipeline(llvm::Module& module, llvm::TargetMachine& machine, int optLevel)
		{
			llvm::LoopAnalysisManager loops;
			llvm::FunctionAnalysisManager functions;
			llvm::CGSCCAnalysisManager sccs;
			llvm::ModuleAnalysisManager modules;
			llvm::PipelineTuningOptions tuning;
			tuning.LoopUnrolling = optLevel >= 2;
			tuning.LoopInterleaving = optLevel >= 2;
			tuning.LoopVectorization = optLevel >= 2;
			tuning.SLPVectorization = optLevel >= 2;
			llvm::PassBuilder builder(&machine, tuning);
			builder.registerModuleAnalyses(modules);
			builder.registerCGSCCAnalyses(sccs);
			builder.registerFunctionAnalyses(functions);
			builder.registerLoopAnalyses(loops);
			builder.crossRegisterProxies(loops, functions, sccs, modules);
			llvm::ModulePassManager pipeline = optLevel == 0 ? builder.buildO0DefaultPipeline(llvm::OptimizationLevel::O0)
				: builder.buildPerModuleDefaultPipeline(PipelineLevel(optLevel));
			pipeline.run(module, modules);
			DropPartialConstantLocations(module);
		}

		void WriteObject(llvm::Module& module, llvm::TargetMachine& machine, const std::string& objPath, std::vector<ObjectDiagnostic>& diagnostics)
		{
			std::error_code ec;
			llvm::raw_fd_ostream out(objPath, ec, llvm::sys::fs::OF_None);
			if (ec)
			{
				diagnostics.push_back({ 0, "cannot write the object file '" + objPath + "': " + ec.message() });
				return;
			}
			llvm::legacy::PassManager backend;
			backend.add(new llvm::TargetLibraryInfoWrapperPass(module.getTargetTriple()));
			backend.add(llvm::createTargetTransformInfoWrapperPass(machine.getTargetIRAnalysis()));
			if (machine.addPassesToEmitFile(backend, out, nullptr, llvm::CodeGenFileType::ObjectFile))
			{
				diagnostics.push_back({ 0, "the target cannot write object files" });
				return;
			}
			backend.run(module);
			out.flush();
		}

		void Assemble(llvm::Module& module, const ObjectSettings& settings, const std::string& objPath, std::vector<ObjectDiagnostic>& diagnostics)
		{
			CollectedDiagnostics collected(module.getContext(), diagnostics);
			std::unique_ptr<llvm::TargetMachine> machine = MachineFor(module, settings, diagnostics);
			if (!machine) return;
			const size_t before = diagnostics.size();
			if (settings.optimizePartitions && settings.optLevel == 0) LowerAggregateCopies(module);
			if (settings.optimizePartitions) RunPipeline(module, *machine, settings.optLevel);
			if (diagnostics.size() == before) WriteObject(module, *machine, objPath, diagnostics);
		}

		// Written beside its final name and moved there once complete, so an interrupted build never leaves a partial
		// object where a later build would take it for a finished one. LLVM's file calls take paths longer than
		// MAX_PATH (a deep build folder), std::filesystem's do not.
		void AssembleInto(llvm::Module& module, const ObjectSettings& settings, const std::string& objPath, std::vector<ObjectDiagnostic>& diagnostics)
		{
			const std::string unfinished = objPath + ".tmp";
			const size_t before = diagnostics.size();
			Assemble(module, settings, unfinished, diagnostics);
			std::error_code ec;
			if (diagnostics.size() == before) ec = llvm::sys::fs::rename(unfinished, objPath);
			if (ec) diagnostics.push_back({ 0, "cannot move the object '" + unfinished + "' into place: " + ec.message() });
			if (diagnostics.size() != before) llvm::sys::fs::remove(unfinished);
		}

		// One partition of the module and the object its code is kept as: its bitcode, or a keyed module's builder.
		struct Partition
		{
			std::string bitcode;
			KeyedModule::Builder build;
			std::string objPath;
			std::vector<ObjectDiagnostic> diagnostics;
			CachedObjects::Assembled assembled;
		};

		double SecondsSince(std::chrono::steady_clock::time_point start)
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		}

		bool Verified(const llvm::Module& module, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics)
		{
			const std::optional<std::string> invalid = settings.verify ? InvalidIr(module) : std::nullopt;
			if (invalid) diagnostics.push_back({ 0, *invalid });
			return !invalid;
		}

		// The partition's module in `context`: built, or read back from its bitcode.
		std::unique_ptr<llvm::Module> ModuleOf(Partition& partition, llvm::LLVMContext& context)
		{
			if (partition.build) return partition.build(context);
			llvm::Expected<std::unique_ptr<llvm::Module>> module = llvm::parseBitcodeFile(
				llvm::MemoryBufferRef(partition.bitcode, partition.objPath), context);
			if (module) return std::move(*module);
			partition.diagnostics.push_back({ 0, "cannot read a partition of the module back: " + llvm::toString(module.takeError()) });
			return nullptr;
		}

		// Assembled on a thread of its own, in a context of its own.
		void AssemblePartition(Partition& partition, const ObjectSettings& settings)
		{
			const auto start = std::chrono::steady_clock::now();
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = ModuleOf(partition, context);
			if (!module) return;
			partition.assembled.instructions = InstructionCount(*module);
			if (!Verified(*module, settings, partition.diagnostics)) return;
			AssembleInto(*module, settings, partition.objPath, partition.diagnostics);
			partition.assembled.seconds = SecondsSince(start);
		}

		// A module kept whole is its own partition, and its extracts are built from it here.
		SplitBitcode Whole(llvm::Module& module, const Extracts& extracts)
		{
			SplitBitcode whole;
			whole.partitions.push_back(ModuleBitcode(module));
			for (const std::vector<const llvm::GlobalValue*>& group : extracts.groups)
				whole.extracted.push_back(extracts.finish(*ExtractDefinitions(module, group)));
			return whole;
		}

		std::vector<Partition> CachedPartitions(llvm::Module& module, const ObjectSettings& settings, unsigned count, const ObjectCache& cache,
			const Extracts& extracts, std::vector<std::string>& extracted)
		{
			const PartitionOptimization optimization = !settings.optimizePartitions ? PartitionOptimization::Before
				: settings.optLevel == 0 ? PartitionOptimization::AfterAtO0 : PartitionOptimization::After;
			SplitBitcode split = count == 1 ? Whole(module, extracts) : SplitModuleAndExtract(module, count, optimization, settings.threads, extracts);
			std::vector<Partition> partitions(split.partitions.size());
			for (size_t i = 0; i < split.partitions.size(); ++i)
			{
				partitions[i].objPath = cache.ObjectFor(split.partitions[i]).string();
				partitions[i].bitcode = std::move(split.partitions[i]);
			}
			extracted = std::move(split.extracted);
			return partitions;
		}

		// The partitions whose objects are missing, each object once (partitions with the same code, empty ones, name
		// the same object: BUG-366), keyed modules first: one is built before it is assembled.
		std::vector<Partition*> NotCached(std::vector<Partition>& partitions)
		{
			std::vector<Partition*> missing;
			std::unordered_set<std::string> named;
			for (Partition& partition : partitions)
				if (named.insert(partition.objPath).second && !llvm::sys::fs::exists(partition.objPath)) missing.push_back(&partition);
			std::stable_partition(missing.begin(), missing.end(), [](const Partition* p) { return static_cast<bool>(p->build); });
			return missing;
		}

		void AssembleInParallel(const std::vector<Partition*>& partitions, const ObjectSettings& settings)
		{
			std::atomic<size_t> next = 0;
			auto worker = [&] {
				for (size_t i = next++; i < partitions.size(); i = next++) AssemblePartition(*partitions[i], settings);
			};
			std::vector<std::thread> threads;
			const size_t count = std::min<size_t>(std::max(settings.threads, 1u), partitions.size());
			for (size_t i = 0; i < count; ++i) threads.emplace_back(worker);
			for (std::thread& thread : threads) thread.join();
		}

		// A module kept whole is assembled from memory; the bitcode was only needed to name its object. The partitions
		// assembled.
		std::vector<Partition*> AssembleMissing(llvm::Module& module, std::vector<Partition>& partitions, const ObjectSettings& settings)
		{
			std::vector<Partition*> missing = NotCached(partitions);
			if (partitions.size() == 1 && missing.size() == 1)
			{
				const auto start = std::chrono::steady_clock::now();
				missing[0]->assembled.instructions = InstructionCount(module);
				if (Verified(module, settings, missing[0]->diagnostics))
					AssembleInto(module, settings, missing[0]->objPath, missing[0]->diagnostics);
				missing[0]->assembled.seconds = SecondsSince(start);
			}
			else AssembleInParallel(missing, settings);
			return missing;
		}

		// The objects of `parts`, or none, with every partition's diagnostics, when one failed; the cache keeps only them.
		CachedObjects Collected(const std::vector<Partition>& parts, const std::vector<Partition*>& assembled, const ObjectCache& cache,
			std::vector<ObjectDiagnostic>& diagnostics)
		{
			const size_t before = diagnostics.size();
			std::vector<std::filesystem::path> used;
			for (const Partition& partition : parts)
			{
				diagnostics.insert(diagnostics.end(), partition.diagnostics.begin(), partition.diagnostics.end());
				used.push_back(partition.objPath);
			}
			if (diagnostics.size() != before) return {};
			cache.KeepOnly(used);
			CachedObjects objects;
			std::unordered_set<std::string> listed; // an object partitions share is linked once
			for (const Partition& partition : parts)
				if (listed.insert(partition.objPath).second) objects.paths.push_back(partition.objPath);
			objects.reused = parts.size() - assembled.size();
			for (const Partition* partition : assembled) objects.assembled.push_back(partition->assembled);
			return objects;
		}
	}

	uint64_t InstructionCount(const llvm::Module& module)
	{
		uint64_t count = 0;
		for (const llvm::Function& function : module) count += function.getInstructionCount();
		return count;
	}

	unsigned PartitionCount(const llvm::Module& module, const ObjectSettings& settings)
	{
		const uint64_t worth = InstructionCount(module) / (settings.optLevel == 0 ? kInstructionsPerPartitionO0 : kInstructionsPerPartition);
		return static_cast<unsigned>(std::bit_floor(std::clamp<uint64_t>(worth, 1, kMaxPartitions)));
	}

	bool OptimizeModule(llvm::Module& module, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics)
	{
		const size_t before = diagnostics.size();
		CollectedDiagnostics collected(module.getContext(), diagnostics);
		std::unique_ptr<llvm::TargetMachine> machine = MachineFor(module, settings, diagnostics);
		if (!machine) return false;
		if (settings.optLevel == 0) LowerAggregateCopies(module);
		RunPipeline(module, *machine, settings.optLevel);
		return diagnostics.size() == before;
	}

	bool WriteObject(llvm::Module& module, const ObjectSettings& settings, const std::string& objPath, std::vector<ObjectDiagnostic>& diagnostics)
	{
		const size_t before = diagnostics.size();
		AssembleInto(module, settings, objPath, diagnostics);
		return diagnostics.size() == before;
	}

	CachedObjects WriteCachedObjects(llvm::Module& module, const ObjectSettings& settings, unsigned partitions,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics, const Extracts& extracts)
	{
		InitializeX86(); // the bitcode that names an object lists its file-scope assembly's symbols only once X86 is in
		const std::error_code ec = llvm::sys::fs::create_directories(cache.Folder().string());
		if (ec)
		{
			diagnostics.push_back({ 0, "cannot create the object cache '" + cache.Folder().string() + "': " + ec.message() });
			return {};
		}
		const auto start = std::chrono::steady_clock::now();
		std::vector<std::string> extracted;
		std::vector<Partition> parts = CachedPartitions(module, settings, partitions, cache, extracts, extracted);
		const double splitSeconds = SecondsSince(start);
		const std::vector<Partition*> assembled = AssembleMissing(module, parts, settings);
		CachedObjects objects = Collected(parts, assembled, cache, diagnostics);
		objects.splitSeconds = splitSeconds;
		objects.extracted = std::move(extracted);
		return objects;
	}

	CachedObjects WriteCachedObjects(std::vector<std::string> partitions, std::vector<KeyedModule> keyed, const ObjectSettings& settings,
		const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics)
	{
		std::vector<std::string> written;
		return WriteCachedObjects(partitions.size(), [&partitions](size_t i) { return std::move(partitions[i]); }, std::move(keyed), settings,
			cache, diagnostics, written);
	}

	CachedObjects WriteCachedObjects(size_t partitions, const std::function<std::string(size_t)>& bitcode, std::vector<KeyedModule> keyed,
		const ObjectSettings& settings, const ObjectCache& cache, std::vector<ObjectDiagnostic>& diagnostics, std::vector<std::string>& written)
	{
		InitializeX86();
		const std::error_code ec = llvm::sys::fs::create_directories(cache.Folder().string());
		if (ec)
		{
			diagnostics.push_back({ 0, "cannot create the object cache '" + cache.Folder().string() + "': " + ec.message() });
			return {};
		}
		std::vector<Partition> parts(partitions + keyed.size());
		for (size_t i = 0; i < keyed.size(); ++i)
		{
			parts[partitions + i].objPath = cache.ObjectKeyed(keyed[i].key).string();
			parts[partitions + i].build = std::move(keyed[i].build);
		}
		std::mutex lock;
		std::unordered_set<std::string> claimed; // each object assembled once: partitions with the same code share it (BUG-366)
		std::vector<char> assembled(parts.size(), 0);
		ParallelFor(parts.size(), std::max(settings.threads, 1u), [&](size_t task) {
			const size_t i = task < keyed.size() ? partitions + task : task - keyed.size();
			Partition& part = parts[i];
			if (i < partitions)
			{
				part.bitcode = bitcode(i);
				part.objPath = cache.ObjectFor(part.bitcode).string();
			}
			{
				const std::lock_guard<std::mutex> held(lock);
				if (!claimed.insert(part.objPath).second) return;
			}
			if (llvm::sys::fs::exists(part.objPath)) return;
			AssemblePartition(part, settings);
			assembled[i] = 1;
		});
		std::vector<Partition*> made;
		for (size_t i = 0; i < parts.size(); ++i)
			if (assembled[i]) made.push_back(&parts[i]);
		CachedObjects objects = Collected(parts, made, cache, diagnostics);
		written.clear();
		for (size_t i = 0; i < partitions; ++i) written.push_back(std::move(parts[i].bitcode));
		return objects;
	}
}
