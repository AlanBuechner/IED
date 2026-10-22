#define IMPLEMENT_LOGGER
#include "Compiler/ProjectFile.h"
#include "Compiler/SourceMacros.h"
#include "Core/Core.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/FileIo.h"
#include "Compiler/Version.h"
#include "Compiler/CompilationResult.h"
#include "Compiler/DiagnosticReporter.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include "ProgramOutput.h"
#include "Macro/PredefinedMacros.h"
#include "Macro/MacroPass.h"
#include "Interop/InteropPass.h"
#include "Parser/Consumer/PrettyPrintConsumer.h"
#include "Parser/Consumer/ASTWalker.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include <optional>
#include <unordered_map>

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>
#include <system_error>
#include <iostream>
#include <cstdio>
#include <sstream>
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
	/// The front-end pipeline, in order:
	///   1. RegisterSources     - resolve the CLI input and register its source files
	///   2. RunPreprocessPhase  - macro expansion + C/C++ interop pre-pass (rewrites source text)
	///   3. CompileAll          - parse all files into ASTs, resolving include dependencies
	///   4. AnalyzeProgram      - build the single program-wide model (docs/compiler/02-program-model.md)
	///   5. RunBackend          - HIR lowering, LLVM codegen, object and link (docs/compiler/04-codegen-and-abi.md)
	/// Diagnostics from every phase are accumulated in a CompilationResult and reported once: at the
	/// first failing phase, or at the end when the build succeeded with warnings.
	double SecondsSince(const std::chrono::steady_clock::time_point& start)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	class CompilerDriver
	{
	public:
		explicit CompilerDriver(const Scripting::CompileOptions& options) : m_Options(options) {}

		int Run()
		{
			if (!RegisterSources())
				return 1;

			m_Result.timePhases = m_Options.timePhases;
			m_Compiler.SetTarget(m_Options.target);

			auto phaseStart = std::chrono::steady_clock::now();
			RunPreprocessPhase();
			m_Compiler.AddPhaseTime("preprocess", SecondsSince(phaseStart));

			// Front end: parse all files, then build the program model.
			CORE_INFO("Phase 1: Parsing");
			phaseStart = std::chrono::steady_clock::now();
			m_Compiler.CompileAll();
			m_Compiler.AddPhaseTime("parse", SecondsSince(phaseStart));

			CORE_INFO("Phase 2: Program model");
			phaseStart = std::chrono::steady_clock::now();
			bool modelOk = m_Compiler.AnalyzeProgram();
			m_Compiler.AddPhaseTime("model", SecondsSince(phaseStart));
			for (const auto& err : m_Compiler.GetAllDiagnostics())
				m_Result.errors.Push(err);
			ApplyWarningOptions();

			PrettyPrintASTs();

			if (!modelOk || m_Result.HasErrors())
			{
				Scripting::DiagnosticReporter::Report(m_Result, &m_Compiler);
				CORE_WARN("Front end reported {0} error(s)", m_Result.errors.Count());
				return 1;
			}

			const auto& model = m_Compiler.GetProgramModel();
			CORE_INFO("Program model built: {0} object types, {1} enum types from {2} files",
				model.ObjectTypes().Count(), model.EnumTypes().Count(), m_Compiler.GetFilesCompleted());

		// --- Phase 3: backend (HIR lowering + LLVM codegen + link) - DEFAULT ----------------------
		// The ProgramModel-driven backend (docs/compiler/04-codegen-and-abi.md): lower every complete body to HIR,
		// emit a verified LLVM module, compile it to an object, and lld-link with the C4 runtimes into the
		// output executable. `--emit-ir` stops after the .ll (no object/link) for inspection.
		CORE_INFO("Phase 3: Codegen");
		if (!RunBackend())
		{
			Scripting::DiagnosticReporter::Report(m_Result, &m_Compiler);
			return 1;
		}

		// The warnings, and the phase times when they were asked for.
		if (!m_Result.errors.Empty() || m_Options.timePhases) Scripting::DiagnosticReporter::Report(m_Result, &m_Compiler);
		return 0;
		}

	private:
		// How many partitions the program is emitted into (D-81): one unless an incremental build links it; then
		// `--partitions`, or as many as its size is worth.
		unsigned EmittedPartitions()
		{
			if (!ProgramOutput::OptimizesPartitions(m_Options)) return 1;
			if (m_Options.partitions) return m_Options.partitions;
			return Scripting::ProgramPlacement::CountFor(m_Compiler.GetProgramModel());
		}

		// -w drops the warnings; -Werror makes each one an error (B22).
		void ApplyWarningOptions()
		{
			Core::Vector<Scripting::CompilationError> kept;
			for (Scripting::CompilationError e : m_Result.errors)
			{
				if (e.severity == Scripting::ErrorSeverity::Warning && m_Options.noWarnings) continue;
				if (e.severity == Scripting::ErrorSeverity::Warning && m_Options.warningsAsErrors) e.severity = Scripting::ErrorSeverity::Error;
				kept.Push(e);
			}
			m_Result.errors = kept;
		}

		// --- Phase 3: backend -------------------------------------------------------------

		void ReportBackendFailure(const Scripting::CodeGenDriver& driver, const std::string& backendError)
		{
			if (driver.LoweringErrors().Empty())
			{
				m_Result.AddBackendError(Core::String(backendError.c_str()));
				return;
			}
			for (const Scripting::CompilationError& e : driver.LoweringErrors())
				m_Result.errors.Push(e);
		}

		// The context and the driver (the lowered HIR, the module) are never freed: the process ends with the build, and
		// freeing them object by object takes a second on a large program (clang's -disable-free does the same).
		bool RunBackend()
		{
			llvm::LLVMContext& ctx = *new llvm::LLVMContext;
			Scripting::CodeGenDriver& driver = *new Scripting::CodeGenDriver(ctx);

			// The entry point the generated main calls: a -test run drives the test harness (the compiler
			// emits a synthetic main that runs every [test] function), otherwise the user's main.
			std::string backendError;
			Scripting::TestHarnessOptions tests;
			tests.jsonPath = m_Options.testJsonPath.c_str();
			tests.fileOf = [this](uint32 sourceId) { return m_Compiler.GetSourceManager().GetInfo(sourceId).displayName; };
			if (m_Options.debugInfo) driver.EnableDebugInfo();
			if (!m_Options.strictAliasing) driver.DisableStrictAliasing();
			if (m_Options.scriptHost) driver.ShareVtables();
			if (ProgramOutput::OptimizesPartitions(m_Options)) driver.VerifyLater();
			if (const unsigned count = EmittedPartitions(); count > 1) driver.SplitInto(count, m_Options.threads, m_Options.optLevel == 0);
			driver.SetReflectionTag(m_Options.reflectTag);
			auto phaseStart = std::chrono::steady_clock::now();
			bool ok = driver.Lower(m_Compiler.GetProgramModelForLowering(), "main", &backendError, m_Options.runTests ? &tests : nullptr);
			m_Compiler.AddPhaseTime("lower", SecondsSince(phaseStart));
			if (!ok)
			{
				ReportBackendFailure(driver, backendError);
				return false;
			}
			if (m_Options.gcStats) driver.InstallGcStatsPrinter();
			for (const Scripting::CompilationError& e : driver.LoweringErrors())
				m_Result.errors.Push(e); // a successful lowering reports warnings only
			ApplyWarningOptions();
			if (m_Result.HasErrors()) return false;
			std::vector<llvm::Module*> partitions;
			for (unsigned i = 0; i < driver.PartitionCount(); ++i) partitions.push_back(&driver.PartitionModule(i));
			return ProgramOutput({ m_Options, m_Result, m_Compiler, m_Interop, m_ExportedMacros }).Write(partitions);
		}

		// --- Phase 1: input registration -------------------------------------------------

		bool RegisterSources()
		{
			uint32 total = 0;
			if (!m_Options.projectsFile.empty())
			{
				const Scripting::ProjectFileResult read = Scripting::ReadProjectFile(fs::path(m_Options.projectsFile.c_str()));
				Scripting::ProjectSetResult checked = read.error.empty() ? Scripting::ProjectSet::Check(read.list) : Scripting::ProjectSetResult{};
				if (!read.error.empty() || !checked.set)
				{
					std::cerr << "error: " << (read.error.empty() ? m_Options.projectsFile.c_str() + std::string(": ") + checked.error : read.error) << "\n";
					return false;
				}
				m_Compiler.GetSourceManager().SetProjects(std::move(*checked.set));
				total += RegisterProjectSources();
			}

			for (uint32 i = 0; i < m_Options.inputs.Count(); ++i)
			{
				const fs::path& input = fs::path(m_Options.inputs[i].c_str());

				if (!Scripting::FileExists(Scripting::PathText(input)))
				{
					std::cerr << "error: Input file/folder not found: " << input << std::endl;
					return false;
				}

				if (Scripting::IsFolder(Scripting::PathText(input)))
				{
					total += m_Compiler.RegisterFolder(input);
					CORE_INFO("Registered source file(s) from folder {0}", input.string());
					continue;
				}

				// A single file is registered together with its folder so that the files it
				// includes can be resolved.
				fs::path parentDir = fs::absolute(input).parent_path();
				if (!parentDir.empty() && Scripting::IsFolder(Scripting::PathText(parentDir)))
					total += m_Compiler.RegisterFolder(parentDir);
				else
					m_Compiler.RegisterFile(fs::absolute(input));

				CORE_INFO("Registered {0} (with its folder)", input.string());
			}

			CORE_INFO("Registered {0} source file(s) total", total);

			if (total == 0)
			{
				std::string what;
				for (uint32 i = 0; i < m_Options.inputs.Count(); ++i)
					what += (i ? ", " : "") + m_Options.inputs[i];
				std::cerr << "error: No C4 source files (.cf/.ch) found in: " << what
					<< ". C4 sources use the .cf or .ch extension; other file types in the folder are ignored.\n";
				return false;
			}

			// Default library (Bedrock): ships with the language in the install (Toolchain::Home), so a vendored
			// compiler copy always finds its own copy. Only the library files the program includes (and what they
			// include) are compiled, so a program that includes none of it compiles none of it and may use any of
			// its names.
			const fs::path bedrockDir = Scripting::Toolchain::Bedrock();
			if (!Scripting::IsFolder(Scripting::PathText(bedrockDir)))
			{
				std::cerr << "error: the default library was not found at " << bedrockDir.string() << " (set C4_HOME to the "
					"folder that holds C4.exe, Bedrock/, C4Runtime/ and toolchain/)\n";
				return false;
			}
			const uint32 count = m_Compiler.RegisterIncludedLibraryFiles(bedrockDir);
			m_LibraryFolder = bedrockDir.string();
			CORE_INFO("Registered {0} default-library file(s) from {1}", count, bedrockDir.string());

			return true;
		}

		// Every project's source folders and files (docs/language/15-projects.md).
		uint32 RegisterProjectSources()
		{
			uint32 total = 0;
			const Scripting::ProjectSet& projects = m_Compiler.GetSourceManager().Projects();
			for (uint32 p = 0; p < projects.Count(); ++p)
				for (const fs::path& source : projects.At(p).sources)
				{
					if (Scripting::IsFolder(Scripting::PathText(source))) total += m_Compiler.RegisterFolder(source);
					else if (m_Compiler.RegisterFile(source)) ++total;
					else std::cerr << "warning: project '" << projects.At(p).name << "' names '" << source.generic_string() << "', which does not exist\n";
				}
			CORE_INFO("Registered the sources of {0} project(s)", projects.Count());
			return total;
		}

		// --- Phase 2: preprocess (macro expansion + interop) ------------------------------

		void RunPreprocessPhase()
		{
			// Macro pre-pass: replaces each registered source's text with its macro-expanded form before
			// parsing, so the real parse still happens once per file. Each file starts with its project's
			// macros (D-59); the default library's files are expanded too.
			auto macroResults = Scripting::ExpandSourceMacros(m_Compiler.GetSourceManager(),
				Scripting::PredefinedMacros(m_Options.runTests, m_Options.target, m_Options.defines));
			m_Compiler.AddMacroDiagnostics(macroResults);
			for (const auto& result : macroResults) m_ExportedMacros[result.absPath] = result.exported;

			RunInteropPrepass(ResolveRootFolders(), macroResults);
			ApplyEffectiveSourceTexts(macroResults, m_Interop.files);
			ExportMacrosIfRequested(macroResults);
		}

		/// The folders the preprocess passes run over: every input directory, or the containing
		/// folder of each single-file input (matching what RegisterSources registered).
		std::vector<std::string> ResolveRootFolders() const
		{
			std::vector<std::string> roots;

			for (uint32 i = 0; i < m_Options.inputs.Count(); ++i)
			{
				const fs::path& input = fs::path(m_Options.inputs[i].c_str());
				if (Scripting::IsFolder(Scripting::PathText(input)))
				{
					roots.push_back(input.string());
					continue;
				}

				fs::path parentDir = fs::absolute(input).parent_path();
				if (!parentDir.empty() && Scripting::IsFolder(Scripting::PathText(parentDir)))
					roots.push_back(parentDir.string());
			}

			return roots;
		}

		/// Interop pre-pass: generate C4 bindings for `extern "c" include` headers and compile
		/// `extern "c" source` files into objects to link. Runs on the macro-expanded text so it
		/// sees the final form of each file before parsing.
		void RunInteropPrepass(const std::vector<std::string>& rootFolders, const std::vector<Scripting::MacroPass::FileResult>& macroResults)
		{
			// A file's headers are found in its project's folders and its dependencies' (D-59).
			const auto& sources = m_Compiler.GetSourceManager();
			std::unordered_map<std::string, uint32> projectOf;
			for (uint32 id = 1; id <= sources.Count(); ++id)
				if (sources.HasInfo(id)) projectOf[sources.GetInfo(id).absPath] = sources.GetInfo(id).project;
			std::vector<Scripting::InteropFileInput> inputs;
			inputs.reserve(macroResults.size());
			for (const auto& mr : macroResults)
			{
				auto owner = projectOf.find(mr.absPath);
				std::vector<std::string> folders = owner != projectOf.end() && !m_Options.projectsFile.empty()
					? sources.FoldersReachedFrom(owner->second) : std::vector<std::string>();
				inputs.push_back({ mr.name, mr.absPath, mr.text, std::move(folders) });
			}

			std::vector<std::string> cliIncludes;
			for (const auto& inc : m_Options.includeDirs)
				cliIncludes.push_back(std::string(inc.c_str()));

			// The build's own folder: two programs built at once never share a foreign object. Absolute, as CBind and clang
			// run in other working folders (BUG-318).
			const fs::path interopFolder = fs::absolute(Scripting::IntermediateFolder(m_Options) / (fs::path(m_Options.outputFile.c_str()).stem().string() + ".interop"));
			const std::error_code ec = Scripting::CreateFolders(interopFolder.string());
			if (ec)
			{
				m_Result.AddBackendError("cannot create the interop folder " + Core::String(interopFolder.string().c_str()) + ": " + Core::String(ec.message().c_str()));
				return;
			}
			const std::string objDir = interopFolder.string();

			// Foreign sources build at the program's optimization level, with its debug information (BUG-285).
			std::vector<std::string> foreignFlags = { "-O" + std::to_string(m_Options.optLevel) };
			if (m_Options.debugInfo) foreignFlags.push_back("-g");
			m_Interop = Scripting::InteropPass::RunFolder(rootFolders, inputs, cliIncludes, objDir, m_Options.target.Flags(), m_Options.verbose,
				false, foreignFlags);

			// A broken foreign C/C++ file (unresolvable path, CBind rejection, failed compile) must fail
			// the build rather than silently produce a binary missing its declarations/objects. The exact
			// Clang diagnostics were already printed by the interop pass; these hard errors gate the build.
			for (const auto& e : m_Interop.errors)
				m_Result.AddBackendError(e);
		}

		/// Replace each registered source's text with its effective form (macro-expanded + interop
		/// bindings) so parsing sees the final content.
		void ApplyEffectiveSourceTexts(const std::vector<Scripting::MacroPass::FileResult>& macroResults,
			const std::vector<Scripting::InteropFileResult>& interopFiles)
		{
			// Match preprocessed text back to sources by UNIQUE identity (normalized absolute path),
			// never by bare display name: a display name is only unique within one root folder, so a
			// user file and a default-library file sharing a basename would otherwise both receive the
			// same (user) text, silently overwriting the library's definitions.
			std::unordered_map<std::string, const std::string*> byAbs;
			for (const auto& mr : macroResults)
				byAbs[mr.absPath] = &mr.text;
			for (const auto& ir : interopFiles)
				byAbs[ir.absPath] = &ir.text; // interop text already includes the macro expansion

			auto& sm = m_Compiler.GetSourceManager();
			for (uint32 id = 1; id <= m_Compiler.SourceCount(); ++id)
			{
				const auto& info = sm.GetInfo(id);
				auto it = byAbs.find(Scripting::MacroIdentity(info));
				if (it != byAbs.end())
					sm.SetSourceText(id, *it->second);
			}
		}

		void ExportMacrosIfRequested(const std::vector<Scripting::MacroPass::FileResult>& macroResults)
		{
			if (!m_Options.exportMacros)
				return;

			std::string outDir = m_Options.outputFile;
			size_t slash = outDir.find_last_of("/\\");
			outDir = (slash != std::string::npos) ? outDir.substr(0, slash) : ".";
			Scripting::MacroPass::ExportTables(macroResults, outDir);
		}

		// --- Phase 3: front end -----------------------------------------------------------

		/// Pretty-print the parsed ASTs (debug only, doesn't affect compilation).
		void PrettyPrintASTs()
		{
			if (!m_Options.prettyPrint)
				return;
			m_Compiler.MergeIncludes();

			for (uint32 id = 1; id <= m_Compiler.SourceCount(); ++id)
			{
				auto unit = m_Compiler.GetCompilationUnit(id);
				if (!unit) continue;

				const auto& fileInfo = m_Compiler.GetSourceManager().GetInfo(id);
				std::cout << std::endl << "--- " << fileInfo.displayName << " ---" << std::endl;

				auto rootAST = unit->GetRootAST();
				if (!rootAST) continue;

				Scripting::PrettyPrintConsumer consumer;
				Scripting::ASTWalker::Walk(rootAST, consumer);
			}
		}

		Scripting::CompileOptions m_Options;
		Scripting::CompilerInstance m_Compiler;
		Scripting::CompilationResult m_Result;
		Scripting::InteropPass::Result m_Interop;
		std::string m_LibraryFolder; // the default library (Bedrock) next to C4.exe, expanded with the inputs
		std::unordered_map<std::string, Scripting::MacroTable> m_ExportedMacros; // by absolute path: what each file exports
	};
}

// A crash of the compiler itself is reported as one, with the faulting offset inside C4.exe (and the stack's),
// which llvm-symbolizer turns into source lines against this build's C4.pdb.
static LONG WINAPI ReportInternalError(EXCEPTION_POINTERS* ep)
{
	const auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
	const auto offset = [base](const void* at) { return (unsigned long long)(reinterpret_cast<uintptr_t>(at) - base); };
	fprintf(stderr, "C4: internal compiler error: exception 0x%lx at C4.exe+0x%llx\n", ep->ExceptionRecord->ExceptionCode,
		offset(ep->ExceptionRecord->ExceptionAddress));
	void* frames[32];
	const USHORT count = CaptureStackBackTrace(1, 32, frames, nullptr);
	for (USHORT i = 0; i < count; ++i) fprintf(stderr, "  C4.exe+0x%llx\n", offset(frames[i]));
	fprintf(stderr, "Please report it with the program that triggered it.\n");
	return EXCEPTION_EXECUTE_HANDLER;
}

// The exit code of a command line C4.exe cannot run: bad options or no input.
constexpr int kUsageError = 2;

int main(int argc, char** argv)
{
	SetUnhandledExceptionFilter(ReportInternalError);

	const Scripting::CompileOptions opts = Scripting::ParseArgs(argc, argv);
	if (!opts.error.empty())
	{
		std::cerr << "error: " << opts.error << std::endl;
		return kUsageError;
	}
	if (opts.printHelp)
	{
		Scripting::PrintUsage();
		return 0;
	}
	if (opts.printVersion)
	{
		std::cout << "C4 " << Scripting::CompilerVersion::Major << "." << Scripting::CompilerVersion::Minor << "." << Scripting::CompilerVersion::Patch << std::endl;
		return 0;
	}
	if (opts.inputs.Count() != 0 && !opts.projectsFile.empty())
	{
		std::cerr << "error: give the folders to compile or --projects, not both: the description names every project's sources" << std::endl;
		return kUsageError;
	}
	if (opts.inputs.Count() == 0 && opts.projectsFile.empty())
	{
		std::cerr << "error: no input: give the folders or files to compile (C4.exe -h lists the options)" << std::endl;
		return kUsageError;
	}
	return (new CompilerDriver(opts))->Run(); // the program and its model go with the process, as the backend's do
}
