#include "TestHosts.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/DiagnosticReporter.h"
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

	namespace
	{
		// Expands every source as C4.exe does and keeps what each exports.
		std::unordered_map<std::string, MacroTable> Preprocess(CompilerInstance& compiler)
		{
			SourceManager& sources = compiler.GetSourceManager();
			const std::vector<MacroPass::FileResult> results = ExpandSourceMacros(sources, PredefinedMacros(false, CpuTarget(), {}));
			std::unordered_map<std::string, MacroTable> exports;
			std::unordered_map<std::string, const std::string*> textOf;
			for (const MacroPass::FileResult& result : results)
			{
				exports[result.absPath] = result.exported;
				textOf[result.absPath] = &result.text;
			}
			for (uint32 id = 1; id <= sources.Count(); ++id)
				if (auto it = textOf.find(MacroIdentity(sources.GetInfo(id))); it != textOf.end()) sources.SetSourceText(id, *it->second);
			return exports;
		}
	}

	TestHost MakeTestHost(const std::vector<std::pair<std::string, std::string>>& files)
	{
		TestHost host;
		CompilerInstance compiler;
		for (const auto& [name, text] : files) compiler.RegisterInMemory(name, text);
		compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
		const std::unordered_map<std::string, MacroTable> exports = Preprocess(compiler);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		for (const CompilationError& e : compiler.GetAllDiagnostics()) host.diagnostics += DiagnosticReporter::Format(e, &compiler);
		llvm::LLVMContext context;
		CodeGenDriver driver(context);
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
		build.symbols = HostSymbols(module, PreludeFunctionSymbols(compiler.GetProgramModel()));
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

	CompiledScript CompileTestScript(const TestHost& host, const std::string& script, bool optimize, const std::vector<std::string>& allow)
	{
		ScriptCompileOptions options;
		options.name = "test-script";
		options.files.push_back({ "script.cf", script });
		options.optimize = optimize;
		options.allow = allow;
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
