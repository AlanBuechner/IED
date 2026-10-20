#include "Compiler/ObjectEmission.h"
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>
#include <memory>
#include <mutex>

namespace Scripting
{
	namespace
	{
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

		std::unique_ptr<llvm::TargetMachine> MakeTargetMachine(const llvm::Module& module, const ObjectSettings& settings, std::string& error)
		{
			const llvm::Triple& triple = module.getTargetTriple();
			const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
			if (!target) return nullptr;
			llvm::TargetOptions options;
			return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(triple, settings.target->Cpu(),
				settings.target->LlvmFeatures(), options, std::nullopt, std::nullopt, CodeGenLevel(settings.optLevel)));
		}

		// The pipeline `clang -O<n>` runs: unrolling and both vectorizers from -O2 on.
		void Optimize(llvm::Module& module, llvm::TargetMachine& machine, int optLevel)
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
		}

		bool WriteObject(llvm::Module& module, llvm::TargetMachine& machine, const std::string& objPath, std::string& error)
		{
			std::error_code ec;
			llvm::raw_fd_ostream out(objPath, ec, llvm::sys::fs::OF_None);
			if (ec)
			{
				error = "cannot write the object file '" + objPath + "': " + ec.message();
				return false;
			}
			llvm::legacy::PassManager backend;
			backend.add(new llvm::TargetLibraryInfoWrapperPass(module.getTargetTriple()));
			backend.add(llvm::createTargetTransformInfoWrapperPass(machine.getTargetIRAnalysis()));
			if (machine.addPassesToEmitFile(backend, out, nullptr, llvm::CodeGenFileType::ObjectFile))
			{
				error = "the target cannot write object files";
				return false;
			}
			backend.run(module);
			out.flush();
			return true;
		}
	}

	bool EmitObject(llvm::Module& module, const std::string& objPath, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics)
	{
		InitializeX86();
		std::string error;
		std::unique_ptr<llvm::TargetMachine> machine = MakeTargetMachine(module, settings, error);
		if (!machine)
		{
			diagnostics.push_back({ 0, "no code generator for '" + module.getTargetTriple().str() + "': " + error });
			return false;
		}
		module.setDataLayout(machine->createDataLayout());
		const size_t before = diagnostics.size();
		module.getContext().setDiagnosticHandler(std::make_unique<Diagnostics>(diagnostics));
		Optimize(module, *machine, settings.optLevel);
		if (!WriteObject(module, *machine, objPath, error)) diagnostics.push_back({ 0, error });
		return diagnostics.size() == before;
	}
}
