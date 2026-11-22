#include "Script/ScriptCompiler.h"
#include "Backend/ProgramBackend.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Compiler/AggregateCopies.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/DiagnosticReporter.h"
#include "Compiler/SourceMacros.h"
#include "Macro/PredefinedMacros.h"
#include "Parser/Nodes/Declarations/ExternDeclarationNode.h"
#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"
#include "Script/HostTable.h"
#include "Script/InlineImport.h"
#include "Script/InterfaceBuilder.h"
#include "Script/ScriptModulePasses.h"
#include "Script/ScriptReach.h"
#include "Target/TargetAbi.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <algorithm>

namespace C4Compiler
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

		// The script's files through what the macro pass makes of them (R8); the interface's are final already.
		void Preprocess(CompilerInstance& compiler, ScriptUnits& units, const CpuTarget& target)
		{
			SourceManager& sources = compiler.GetSourceManager();
			const std::vector<MacroPass::FileResult> results = ExpandScriptMacros(sources, PredefinedMacros(false, target, {}), units.scriptUnits, units.interfaceMacros);
			compiler.AddMacroDiagnostics(results);
			std::unordered_map<std::string, const MacroPass::FileResult*> resultOf;
			for (const MacroPass::FileResult& result : results) resultOf[result.absPath] = &result;
			for (uint32 id : units.scriptUnits)
				if (auto it = resultOf.find(MacroIdentity(sources.GetInfo(id))); it != resultOf.end())
				{
					sources.SetMacroLayout(id, it->second->layout);
					units.exportedMacros[id] = it->second->exported;
				}
		}

		// A script reaches C and C++ through the bindings its host imported: importing needs CBind and clang.
		void CheckImports(CompilerInstance& compiler, const ScriptUnits& units, ErrorTracker& errors)
		{
			for (uint32 id : units.scriptUnits)
				for (const Ref<AstNode>& node : compiler.GetCompilationUnit(id)->GetOriginalChildren())
					if (dynamic_cast<const ExternDeclarationNode*>(node.Get()))
						errors.AddError(node->startLocation, "a script cannot import C or C++ ('extern' include or source): it uses the bindings its host imported");
		}

		// Whether a script file may include the interface unit `target`: the host's by IncludeAllowed, a layer's by its
		// project or its path, as the host's.
		bool UnitAllowed(const HostInterface& host, const ScriptChain* base, uint32 target, const ScriptCompileOptions& options)
		{
			const std::vector<std::string>& allow = options.allow;
			if (target <= host.files.size()) return IncludeAllowed(host, host.files[target - 1], allow, options.onlyAllowed);
			if (allow.empty()) return !options.onlyAllowed;
			for (const ScriptChain* at : LayersOf(base))
				for (const InterfaceFile& file : at->layer.files)
				{
					if (file.unitId != target) continue;
					for (const std::string& entry : allow)
						if (at->layer.project == entry || file.path.compare(0, entry.size(), entry) == 0) return true;
					return false;
				}
			return false;
		}

		void CheckIncludes(CompilerInstance& compiler, const HostInterface& host, const ScriptUnits& units, const ScriptCompileOptions& options,
			ErrorTracker& errors)
		{
			const SourceManager& sources = compiler.GetSourceManager();
			for (uint32 id : units.scriptUnits)
				for (const Ref<AstNode>& node : compiler.GetCompilationUnit(id)->GetOriginalChildren())
				{
					const auto* include = dynamic_cast<const IncludeDeclarationNode*>(node.Get());
					if (!include) continue;
					const uint32 target = sources.ResolveInclude(include->FilePath().c_str(), id).id;
					if (target >= 1 && target <= units.interfaceUnits && !UnitAllowed(host, options.base.get(), target, options))
						errors.AddError(include->startLocation, "a script may not include '" + include->FilePath() + "': it is not on the allow-list");
				}
		}

		// A function the host defines is the host's: a script giving it a body would split it in two.
		void CheckDefinitions(const ProgramModel& model, const AskedNames& hostSymbols, const ScriptUnits& units, ErrorTracker& errors)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
			{
				if (!record->bodyNode || record->templateOrigin != TemplateOrigin::None || record->isLocal) continue;
				if (record->definingUnit <= units.interfaceUnits || !hostSymbols.Has(record->mangledName.c_str())) continue;
				errors.AddError(record->bodyNode->startLocation, "a script cannot define '" + record->name + "': its host defines it");
			}
		}

		// The units a compile parses: the script's files and what its project reaches (ReachedUnits).
		std::vector<uint32> ParseRoots(const ScriptUnits& units, const std::vector<uint32>& reached)
		{
			std::vector<uint32> roots = reached;
			roots.insert(roots.end(), units.scriptUnits.begin(), units.scriptUnits.end());
			return roots;
		}

		// Which of the functions sharing a symbol gets `_<n>` follows from the order their records came in, so a model of
		// part of the interface names them as the host did only when it read every unit of each such symbol it read one of
		// (ReachedUnits reads them so), and when no function of the script's own has a symbol the host's table names: a
		// host function in a file left out may have had it first. Then only a model of every file says.
		bool NeedsWholeInterface(const CompilerInstance& compiler, const HostInterface& host, const std::vector<const ScriptChain*>& layers,
			const ScriptUnits& units, const AskedNames& hostSymbols)
		{
			auto parsed = [&compiler](uint32 unit) { return compiler.FindCompilationUnit(unit) != nullptr; };
			auto split = [&parsed](const CollidedSymbol& symbol) {
				return std::any_of(symbol.units.begin(), symbol.units.end(), parsed) && !std::all_of(symbol.units.begin(), symbol.units.end(), parsed);
			};
			if (std::any_of(host.collidedSymbols.begin(), host.collidedSymbols.end(), split)) return true;
			for (const ScriptChain* layer : layers)
				if (std::any_of(layer->layer.collidedSymbols.begin(), layer->layer.collidedSymbols.end(), split)) return true;
			Core::Vector<const FunctionRecord*> records;
			compiler.GetProgramModel().AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
				if (record->declUnit > units.interfaceUnits && !record->isLinked && hostSymbols.Has(record->mangledName.c_str())) return true;
			return false;
		}

		size_t UnitsParsed(const CompilerInstance& compiler, uint32 interfaceUnits)
		{
			size_t parsed = 0;
			for (uint32 id = 1; id <= interfaceUnits; ++id) parsed += compiler.FindCompilationUnit(id) ? 1 : 0;
			return parsed;
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

		// The script's model in `compiler`: of what its project reaches, or, when that meets a collided symbol, of every
		// file in a compiler of its own.
		bool FrontEnd(std::unique_ptr<CompilerInstance>& compiler, const HostInterface& host, const HostIndex& index, const HostQuestions& questions,
			const ScriptCompileOptions& options, ScriptUnits& units, CompiledScript& out)
		{
			std::string error;
			ErrorTracker reachedChecks;
			ErrorTracker wholeChecks;
			std::optional<ScriptUnits> analyzed = AnalyzeScript(*compiler, host, index, questions, options, InterfaceParse::Reached, reachedChecks, &error);
			const bool whole = analyzed && analyzed->needsWholeInterface;
			if (whole)
			{
				compiler = std::make_unique<CompilerInstance>();
				analyzed = AnalyzeScript(*compiler, host, index, questions, options, InterfaceParse::Whole, wholeChecks, &error);
			}
			const ErrorTracker& checks = whole ? wholeChecks : reachedChecks;
			if (!analyzed)
			{
				out.diagnostics = "error: " + error + "\n";
				return false;
			}
			units = std::move(*analyzed);
			out.interfaceFilesRead = UnitsParsed(*compiler, units.interfaceUnits);
			out.readWholeInterface = whole;
			Core::Vector<CompilationError> diagnostics = compiler->GetAllDiagnostics();
			for (const CompilationError& e : checks.GetErrors()) diagnostics.Push(e);
			out.diagnostics = Format(diagnostics, *compiler);
			return !Failed(diagnostics);
		}

		// The host's inline bodies the script reaches, linked in for inlining, each kept as an assumption with its digest.
		bool ImportBodies(llvm::Module& module, const HostInterface& host, const HostIndex& index, HostAssumptions& assumptions, std::string& error)
		{
			std::vector<std::string> imported;
			if (!ImportInlineBodies(module, host.inlineBodies, *index.inlineBodies, imported, error)) return false;
			for (const std::string& name : imported)
			{
				std::string digest = index.inlineBodies->Digest(name, host.inlineBodies, error);
				if (!error.empty()) return false;
				assumptions.inlineBodies.emplace_back(name, std::move(digest));
			}
			return true;
		}
	}

	std::optional<ScriptUnits> AnalyzeScript(CompilerInstance& compiler, const HostInterface& host, const HostIndex& index,
		const HostQuestions& questions, const ScriptCompileOptions& options, InterfaceParse parse, ErrorTracker& checks, std::string* error)
	{
		const std::vector<const ScriptChain*> layers = LayersOf(options.base.get());
		const std::vector<std::string> depends = ScriptDependencies(host, layers, options.allow, options.onlyAllowed);
		const ScriptSourcesDesc desc{ options.name, options.files, options.defines, options.project, options.roots, options.base.get(), depends };
		std::optional<ScriptUnits> units = RegisterScriptSources(compiler, host, desc, error);
		if (!units) return std::nullopt;
		units->depends = depends;
		compiler.SetTarget(index.target);
		Preprocess(compiler, *units, index.target);
		compiler.SetHostSymbols(&questions.symbols);
		if (parse == InterfaceParse::Reached) compiler.CompileReachable(ParseRoots(*units, ReachedUnits(host, layers, depends)));
		else compiler.CompileAll();
		compiler.AnalyzeProgram();
		if (parse == InterfaceParse::Reached && NeedsWholeInterface(compiler, host, layers, *units, questions.symbols))
		{
			units->needsWholeInterface = true;
			return units;
		}
		CheckImports(compiler, *units, checks);
		CheckIncludes(compiler, host, *units, options, checks);
		CheckDefinitions(compiler.GetProgramModel(), questions.symbols, *units, checks);
		return units;
	}

	HostIndex::HostIndex(const HostInterface& host)
		: symbols(host.symbols.begin(), host.symbols.end()), types(host.reflectedTypes.begin(), host.reflectedTypes.end()), target(TargetOf(host)) {}

	bool IncludeAllowed(const HostInterface& host, const InterfaceFile& include, const std::vector<std::string>& allow, bool onlyAllowed)
	{
		if (include.library || (allow.empty() && !onlyAllowed)) return true;
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
		auto compiler = std::make_unique<CompilerInstance>();
		const HostQuestions questions(index);
		ScriptUnits units;
		if (!FrontEnd(compiler, host, index, questions, options, units, out)) return out;

		out.context = std::make_unique<llvm::LLVMContext>();
		std::string error;
		std::unique_ptr<llvm::Module> module;
		{
			ProgramBackend driver(*out.context, HostSystem()); // a script runs in the host that compiles it
			driver.SetReflectionTag(options.name.c_str());
			driver.EnableScriptMode({ &questions.symbols, &questions.types, units.interfaceUnits });
			if (options.debugInfo) driver.EnableDebugInfo();
			TestHarnessOptions tests;
			tests.entry = kScriptTestsSymbol;
			tests.ownUnit = [host = units.interfaceUnits](uint32 unit) { return unit > host; }; // units 1..host are the host's, tests and all
			tests.fileOf = [&compiler](uint32 sourceId) { return std::string(compiler->GetSourceManager().GetInfo(sourceId).displayName); };
			if (!driver.Lower(compiler->GetProgramModelForLowering(), "", &error, &tests))
			{
				Core::Vector<CompilationError> lowering = driver.LoweringErrors();
				out.diagnostics += Format(lowering, *compiler);
				if (!error.empty() && lowering.Empty()) out.diagnostics += "error: " + error + "\n";
				return out;
			}
			out.diagnostics += Format(driver.LoweringErrors(), *compiler); // warnings
			module = driver.TakeModule();
		}
		if (options.extensible)
		{
			ScriptLayer layer;
			layer.project = compiler->GetSourceManager().Projects().At(units.scriptProject).name;
			layer.folders = units.scriptFolders;
			layer.depends = units.depends;
			layer.collidedSymbols = CollidedSymbols(compiler->GetProgramModel());
			std::optional<std::vector<InterfaceFile>> files = LayerFiles(*compiler, units.scriptUnits, units.exportedMacros, layer.folders, &error);
			if (!files)
			{
				out.diagnostics += "error: " + error + "\n";
				return out;
			}
			layer.files = std::move(*files);
			for (std::string& type : ReflectedTypeNames(compiler->GetProgramModelForLowering()))
				if (!questions.types.Has(type)) layer.reflectedTypes.push_back(std::move(type));
			out.layer = std::move(layer);
			ExportLayerDefinitions(*module);
		}
		out.threadLocals = ThreadLocals(*module);
		// The host's bodies are only for inlining (docs/runtime/04 section 3.3): an unoptimized script takes none.
		if (options.optimize && !ImportBodies(*module, host, index, out.assumptions, error))
		{
			out.diagnostics += "error: " + error + "\n";
			return out;
		}
		Optimize(*module, machine, options.optimize);
		KeepImageOffsetsInImage(*module);
		// ELF: each table reference relative to its own field, once the host functions they name are thunks in the
		// script (a script lies anywhere in the address space, too far from the host for a 32-bit offset).
		TargetAbi::For(HostSystem()).AnchorTables(*module);
		if (out.layer) out.layer->symbols = LayerSymbols(*module);
		out.assumptions.symbols = questions.symbols.Asked();
		out.assumptions.types = questions.types.Asked();
		out.module = std::move(module);
		return out;
	}
} // namespace C4Compiler
