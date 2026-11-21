#include "TestHosts.h"
#include "Backend/ProgramBackend.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/DiagnosticReporter.h"
#include "Compiler/Projects.h"
#include "Compiler/SourceMacros.h"
#include "Compiler/Toolchain.h"
#include "Macro/PredefinedMacros.h"
#include "Script/HostTable.h"
#include "Script/InlineBodies.h"
#include "Script/InterfaceBuilder.h"
#include "Script/ScriptJit.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <unordered_map>

namespace Testing
{
	using namespace Scripting;

	TestHost BuildTestHost(CompilerInstance& compiler);

	namespace
	{
		// Runs the macro pass over every source as C4.exe does and keeps what each exports.
		std::unordered_map<std::string, MacroTable> Preprocess(CompilerInstance& compiler)
		{
			SourceManager& sources = compiler.GetSourceManager();
			const std::vector<MacroPass::FileResult> results = ExpandSourceMacros(sources, PredefinedMacros(false, CpuTarget(), {}));
			std::unordered_map<std::string, MacroTable> exports;
			for (const MacroPass::FileResult& result : results) exports[result.absPath] = result.exported;
			ApplyMacroLayouts(sources, results);
			return exports;
		}
	}

	TestHost MakeTestHost(const std::vector<std::pair<std::string, std::string>>& files)
	{
		CompilerInstance compiler;
		for (const auto& [name, text] : files) compiler.RegisterInMemory(name, text);
		return BuildTestHost(compiler);
	}

	TestHost MakeProjectTestHost(const std::vector<TestProject>& projects)
	{
		CompilerInstance compiler;
		SourceManager& sources = compiler.GetSourceManager();
		ProjectList list;
		for (const TestProject& project : projects) list.projects.push_back({ project.name, {}, {}, {}, project.depends });
		list.root = projects.back().name;
		ProjectSetResult checked = ProjectSet::Check(std::move(list));
		if (!checked.set)
		{
			TestHost failed;
			failed.diagnostics = checked.error;
			return failed;
		}
		sources.SetProjects(std::move(*checked.set));
		for (uint32 p = 0; p < projects.size(); ++p)
		{
			const std::string folder = "//c4test/" + projects[p].name;
			sources.AddFolder(folder, p);
			for (const auto& [name, text] : projects[p].files) sources.RegisterVirtual(folder + "/" + name, name, text, p, false);
		}
		return BuildTestHost(compiler);
	}

	TestHost BuildTestHost(CompilerInstance& compiler)
	{
		TestHost host;
		compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
		const std::unordered_map<std::string, MacroTable> exports = Preprocess(compiler);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		for (const CompilationError& e : compiler.GetAllDiagnostics()) host.diagnostics += DiagnosticReporter::Format(e, &compiler);
		llvm::LLVMContext context;
		ProgramBackend driver(context);
		driver.ShareVtables();
		std::string error;
		if (!driver.Lower(compiler.GetProgramModelForLowering(), "main", &error))
		{
			host.diagnostics += "lowering: " + error;
			return host;
		}
		llvm::Module& module = *driver.module();
		EmitThreadLocalAccessors(module);
		HostBuild build;
		std::vector<std::string> listed = PreludeFunctionSymbols(compiler.GetProgramModel());
		const std::vector<std::string> runtime = LibraryRuntimeSymbols(compiler.GetProgramModel(), compiler.GetSourceManager());
		listed.insert(listed.end(), runtime.begin(), runtime.end());
		build.symbols = HostSymbols(module, listed);
		build.inlineBodies = InlineBodies(module, build.symbols);
		build.reflectedTypes = ReflectedTypeNames(compiler.GetProgramModel());
		std::optional<HostInterface> built = BuildInterface(compiler, exports, std::move(build), &error);
		if (!built)
		{
			host.diagnostics += error;
			return host;
		}
		host.interface = std::move(*built);
		host.index = std::make_unique<HostIndex>(host.interface);
		return host;
	}

	CompiledScript CompileTestScript(const TestHost& host, const std::string& script, bool optimize, const std::vector<std::string>& allow, bool onlyAllowed)
	{
		ScriptCompileOptions options;
		options.name = "test-script";
		options.files.push_back({ "script.cf", script });
		options.optimize = optimize;
		options.allow = allow;
		options.onlyAllowed = onlyAllowed;
		std::string error;
		std::unique_ptr<llvm::TargetMachine> machine = ScriptTargetMachine(host.index->target, &error);
		if (!machine)
		{
			CompiledScript none;
			none.diagnostics = error;
			return none;
		}
		return CompileScript(host.interface, *host.index, options, *machine);
	}
} // namespace Testing
