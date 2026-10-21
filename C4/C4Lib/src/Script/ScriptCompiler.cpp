#include "Script/ScriptCompiler.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Compiler/AggregateCopies.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/DiagnosticReporter.h"
#include "Compiler/SourceMacros.h"
#include "Macro/PredefinedMacros.h"
#include "Parser/Nodes/Declarations/ExternDeclarationNode.h"
#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"
#include "Script/InlineImport.h"
#include "Script/ScriptModulePasses.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>

namespace Scripting
{
	namespace
	{
		std::string Format(const Core::Vector<CompilationError>& errors, const CompilerInstance& compiler)
		{
			std::string text;
			for (const CompilationError& e : errors) text += DiagnosticReporter::Format(e, &compiler);
			return text;
		}

		bool Failed(const Core::Vector<CompilationError>& errors)
		{
			for (const CompilationError& e : errors)
				if (e.severity != ErrorSeverity::Warning) return true;
			return false;
		}

		// The interface's target; reading the interface checked that it is one (DeserializeInterface).
		CpuTarget TargetOf(const HostInterface& host)
		{
			std::string unused;
			return CpuTarget::FromLlvm(host.targetCpu, host.targetFeatures, unused).value_or(CpuTarget());
		}

		// The script's files as the macro pass expands them; the interface's are final already.
		void Preprocess(CompilerInstance& compiler, const ScriptUnits& units, const CpuTarget& target)
		{
			SourceManager& sources = compiler.GetSourceManager();
			const std::vector<MacroPass::FileResult> results = ExpandSourceMacros(sources, PredefinedMacros(false, target, {}), units.interfaceMacros);
			compiler.AddMacroDiagnostics(results);
			std::unordered_map<std::string, const std::string*> textOf;
			for (const MacroPass::FileResult& result : results) textOf[result.absPath] = &result.text;
			for (uint32 id : units.scriptUnits)
				if (auto it = textOf.find(MacroIdentity(sources.GetInfo(id))); it != textOf.end()) sources.SetSourceText(id, *it->second);
		}

		// A script reaches C and C++ through the bindings its host imported: importing needs CBind and clang.
		void CheckImports(CompilerInstance& compiler, const ScriptUnits& units, ErrorTracker& errors)
		{
			for (uint32 id : units.scriptUnits)
				for (const Ref<AstNode>& node : compiler.GetCompilationUnit(id)->GetOriginalChildren())
					if (dynamic_cast<const ExternDeclarationNode*>(node.Get()))
						errors.AddError(node->startLocation, "a script cannot import C or C++ ('extern' include or source): it uses the bindings its host imported");
		}

		void CheckIncludes(CompilerInstance& compiler, const HostInterface& host, const ScriptUnits& units, const std::vector<std::string>& allow,
			ErrorTracker& errors)
		{
			const SourceManager& sources = compiler.GetSourceManager();
			for (uint32 id : units.scriptUnits)
				for (const Ref<AstNode>& node : compiler.GetCompilationUnit(id)->GetOriginalChildren())
				{
					const auto* include = dynamic_cast<const IncludeDeclarationNode*>(node.Get());
					if (!include) continue;
					const uint32 target = sources.ResolveInclude(include->FilePath().c_str(), id).id;
					if (target >= 1 && target <= units.interfaceUnits && !IncludeAllowed(host, host.files[target - 1], allow))
						errors.AddError(include->startLocation, "a script may not include '" + include->FilePath() + "': it is not on the allow-list");
				}
		}

		// A function the host defines is the host's: a script giving it a body would split it in two.
		void CheckDefinitions(const ProgramModel& model, const HostIndex& index, const ScriptUnits& units, ErrorTracker& errors)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
			{
				if (!record->bodyNode || record->templateOrigin != TemplateOrigin::None || record->isLocal) continue;
				if (record->definingUnit <= units.interfaceUnits || !index.symbols.count(record->mangledName.c_str())) continue;
				errors.AddError(record->bodyNode->startLocation, "a script cannot define '" + record->name + "': its host defines it");
			}
		}

		std::vector<std::string> ThreadLocals(const llvm::Module& module)
		{
			std::vector<std::string> names;
			for (const llvm::GlobalVariable& g : module.globals())
				if (g.isThreadLocal() && !g.isDeclaration()) names.push_back(g.getName().str());
			return names;
		}

		void Optimize(llvm::Module& module, llvm::TargetMachine& machine, bool optimize)
		{
			llvm::LoopAnalysisManager loops;
			llvm::FunctionAnalysisManager functions;
			llvm::CGSCCAnalysisManager sccs;
			llvm::ModuleAnalysisManager modules;
			llvm::PipelineTuningOptions tuning; // as clang -O2: both vectorizers, unrolling, interleaving
			tuning.LoopVectorization = optimize;
			tuning.SLPVectorization = optimize;
			tuning.LoopUnrolling = optimize;
			tuning.LoopInterleaving = optimize;
			llvm::PassBuilder builder(&machine, tuning);
			builder.registerModuleAnalyses(modules);
			builder.registerCGSCCAnalyses(sccs);
			builder.registerFunctionAnalyses(functions);
			builder.registerLoopAnalyses(loops);
			builder.crossRegisterProxies(loops, functions, sccs, modules);
			if (!optimize) LowerAggregateCopies(module);
			llvm::ModulePassManager pipeline = optimize ? builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2)
				: builder.buildO0DefaultPipeline(llvm::OptimizationLevel::O0);
			pipeline.run(module, modules);
		}

		bool FrontEnd(CompilerInstance& compiler, const HostInterface& host, const HostIndex& index, const ScriptCompileOptions& options,
			ScriptUnits& units, CompiledScript& out)
		{
			std::string error;
			ErrorTracker checks;
			std::optional<ScriptUnits> analyzed = AnalyzeScript(compiler, host, index, options, checks, &error);
			if (!analyzed)
			{
				out.diagnostics = "error: " + error + "\n";
				return false;
			}
			units = std::move(*analyzed);
			Core::Vector<CompilationError> diagnostics = compiler.GetAllDiagnostics();
			for (const CompilationError& e : checks.GetErrors()) diagnostics.Push(e);
			out.diagnostics = Format(diagnostics, compiler);
			return !Failed(diagnostics);
		}
	}

	std::optional<ScriptUnits> AnalyzeScript(CompilerInstance& compiler, const HostInterface& host, const HostIndex& index,
		const ScriptCompileOptions& options, ErrorTracker& checks, std::string* error)
	{
		std::optional<ScriptUnits> units = RegisterScriptSources(compiler, host, options.name, options.files, options.defines, error);
		if (!units) return std::nullopt;
		compiler.SetTarget(index.target);
		Preprocess(compiler, *units, index.target);
		compiler.SetHostSymbols(&index.symbols);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		CheckImports(compiler, *units, checks);
		CheckIncludes(compiler, host, *units, options.allow, checks);
		CheckDefinitions(compiler.GetProgramModel(), index, *units, checks);
		return units;
	}

	HostIndex::HostIndex(const HostInterface& host)
		: symbols(host.symbols.begin(), host.symbols.end()), types(host.reflectedTypes.begin(), host.reflectedTypes.end()), target(TargetOf(host)) {}

	bool IncludeAllowed(const HostInterface& host, const InterfaceFile& include, const std::vector<std::string>& allow)
	{
		if (include.library || allow.empty()) return true;
		for (const std::string& entry : allow)
		{
			if (include.project < host.projects.size() && host.projects[include.project].name == entry) return true;
			if (include.path.compare(0, entry.size(), entry) == 0) return true;
		}
		return false;
	}

	CompiledScript CompileScript(const HostInterface& host, const HostIndex& index, const ScriptCompileOptions& options, llvm::TargetMachine& machine)
	{
		CompiledScript out;
		CompilerInstance compiler;
		ScriptUnits units;
		if (!FrontEnd(compiler, host, index, options, units, out)) return out;

		out.context = std::make_unique<llvm::LLVMContext>();
		std::string error;
		std::unique_ptr<llvm::Module> module;
		{
			CodeGenDriver driver(*out.context);
			driver.SetReflectionTag(options.name.c_str());
			driver.EnableScriptMode({ &index.symbols, &index.types, units.interfaceUnits });
			TestHarnessOptions tests;
			tests.entry = kScriptTestsSymbol;
			tests.hostUnits = units.interfaceUnits;
			tests.fileOf = [&compiler](uint32 sourceId) { return std::string(compiler.GetSourceManager().GetInfo(sourceId).displayName); };
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "", &error, &tests))
			{
				Core::Vector<CompilationError> lowering = driver.LoweringErrors();
				out.diagnostics += Format(lowering, compiler);
				if (!error.empty() && lowering.Empty()) out.diagnostics += "error: " + error + "\n";
				return out;
			}
			out.diagnostics += Format(driver.LoweringErrors(), compiler); // warnings
			module = driver.TakeModule();
		}
		out.threadLocals = ThreadLocals(*module);
		// The host's bodies are only for inlining (docs/runtime/04 section 3.3): an unoptimized script takes none.
		if (options.optimize && !ImportInlineBodies(*module, host.inlineBodies, error))
		{
			out.diagnostics += "error: " + error + "\n";
			return out;
		}
		Optimize(*module, machine, options.optimize);
		KeepImageOffsetsInImage(*module);
		out.module = std::move(module);
		return out;
	}
} // namespace Scripting
