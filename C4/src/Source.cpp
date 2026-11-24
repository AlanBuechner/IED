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
#include "Compiler/PriorParses.h"
#include "Compiler/Toolchain.h"
#include "ProgramOutput.h"
#include "ServeChannel.h"
#include "Macro/PredefinedMacros.h"
#include "Macro/MacroPass.h"
#include "Interop/InteropPass.h"
#include "Interop/InteropSources.h"
#include "Parser/Consumer/PrettyPrintConsumer.h"
#include "Parser/Consumer/ASTWalker.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Backend/ProgramBackend.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Serve/BodySwap.h"
#include <algorithm>
#include <memory>
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
#include <string_view>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <llvm/Support/Signals.h>
#endif

namespace fs = std::filesystem;

namespace
{
	/// The front-end pipeline, in order:
	///   1. RegisterSources     - resolve the CLI input and register its source files
	///   2. RunPreprocessPhase  - the macro pass (each file's MacroLayout) + the C/C++ interop pass (binding units)
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
		// `prior`: the parses of the build before (--serve), which a file of the same text takes instead of parsing again.
		CompilerDriver(const C4Compiler::CompileOptions& options, std::shared_ptr<const C4Compiler::PriorParses> prior)
			: m_Options(options)
		{
			m_Compiler.UsePriorParses(std::move(prior));
		}

		// This build's parses, for the next build to take.
		std::shared_ptr<const C4Compiler::PriorParses> Parses() const { return m_Compiler.Parses(); }

		int Run()
		{
			return Prepare() ? Finish() : 1;
		}

		// What a build reads before its model: the sources registered, the macro and interop passes, every file parsed.
		// False when the inputs cannot be registered (said on stderr).
		bool Prepare()
		{
			auto registered = std::chrono::steady_clock::now();
			if (!RegisterSources())
				return false;
			m_Compiler.AddPhaseTime("register", SecondsSince(registered));

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
			return true;
		}

		// After Prepare: the model, the backend and the output; the build's exit code.
		int Finish()
		{
			m_Code = BuildFromParses();
			return m_Code;
		}

		int BuildFromParses()
		{
			CORE_INFO("Phase 2: Program model");
			auto phaseStart = std::chrono::steady_clock::now();
			bool modelOk = m_Compiler.AnalyzeProgram();
			m_Compiler.AddPhaseTime("model", SecondsSince(phaseStart));
			for (const auto& err : m_Compiler.GetAllDiagnostics())
				m_Result.errors.Push(err);
			ApplyWarningOptions();

			PrettyPrintASTs();

			if (!modelOk || m_Result.HasErrors())
			{
				C4Compiler::DiagnosticReporter::Report(m_Result, &m_Compiler);
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
			C4Compiler::DiagnosticReporter::Report(m_Result, &m_Compiler);
			return 1;
		}

		// The warnings, and the phase times when they were asked for.
		if (!m_Result.errors.Empty() || m_Options.timePhases) C4Compiler::DiagnosticReporter::Report(m_Result, &m_Compiler);
		return 0;
		}

		// --serve, after an edit (docs/compiler/07 stages 2a, 2b): `edited` prepared the sources as they are now with this
		// build's parses. When this build succeeded and the edit changed only bodies their own functions read and the values
		// of globals, this build takes the edit - the globals folded again, the changed bodies and those that read a changed
		// value lowered again in its model - and emits and links the program again: its exit code. Nothing otherwise (said
		// with -v or -ftime), and this build is spent: `edited` finishes a cold build.
		std::optional<int> BuildEdit(CompilerDriver& edited)
		{
			if (m_Code != 0 || !m_Backend || m_Options.prettyPrint || edited.m_Result.HasErrors()) return std::nullopt;
			const auto start = std::chrono::steady_clock::now();
			if (const std::string why = C4Compiler::SwapEdits(m_Compiler, edited.m_Compiler, *m_Backend, m_Retired); !why.empty())
			{
				if (m_Options.verbose || m_Options.timePhases) std::printf("serve: a cold build: %s\n", why.c_str());
				m_Code = 1;
				return std::nullopt;
			}
			m_Interop = std::move(edited.m_Interop);
			m_ExportedMacros = std::move(edited.m_ExportedMacros);
			m_Compiler.ClearPhaseTimes();
			for (const auto& [phase, seconds] : edited.m_Compiler.GetPhaseTimes()) m_Compiler.AddPhaseTime(phase, seconds);
			m_Compiler.AddPhaseTime("bodies again", SecondsSince(start));
			m_Result = C4Compiler::CompilationResult{};
			m_Result.timePhases = m_Options.timePhases;
			for (const auto& err : m_Compiler.GetAllDiagnostics()) m_Result.errors.Push(err);
			ApplyWarningOptions();
			if (m_Result.HasErrors() || !EmitAgain())
			{
				C4Compiler::DiagnosticReporter::Report(m_Result, &m_Compiler);
				m_Code = 1;
				return m_Code;
			}
			if (!m_Result.errors.Empty() || m_Options.timePhases) C4Compiler::DiagnosticReporter::Report(m_Result, &m_Compiler);
			m_Code = 0;
			return m_Code;
		}

	private:
		// A whole-program build's partitions are merged again before they are optimized (D-90): enough to emit on every
		// thread, few enough that the merge stays short (each partition carries every type the program lays out). A fixed
		// number, so the merged module, and the objects split from it, do not depend on -j.
		static constexpr unsigned c_MergedPartitions = 16;

		// How many partitions the program is emitted into (D-81, D-90): one for -S, -bc and -c, which write the module;
		// else `--partitions`, or as many as its size is worth - at most c_MergedPartitions when they are merged.
		unsigned EmittedPartitions()
		{
			if (!ProgramOutput::Links(m_Options)) return 1;
			if (m_Options.partitions) return m_Options.partitions;
			const unsigned worth = C4Compiler::ProgramPlacement::CountFor(m_Compiler.GetProgramModel());
			return m_Options.incremental ? worth : std::min(worth, c_MergedPartitions);
		}

		// What a partition copies from the others to inline: nothing when the partitions are merged before they are
		// optimized (a whole-program build), only `inline` functions at -O0.
		C4Compiler::PartitionCopies CopiesBetweenPartitions() const
		{
			if (!m_Options.incremental) return C4Compiler::PartitionCopies::None;
			return m_Options.optLevel == 0 ? C4Compiler::PartitionCopies::Inline : C4Compiler::PartitionCopies::Small;
		}

		// -w drops the warnings; -Werror makes each one an error (B22).
		void ApplyWarningOptions()
		{
			Core::Vector<C4Compiler::CompilationError> kept;
			for (C4Compiler::CompilationError e : m_Result.errors)
			{
				if (e.severity == C4Compiler::ErrorSeverity::Warning && m_Options.noWarnings) continue;
				if (e.severity == C4Compiler::ErrorSeverity::Warning && m_Options.warningsAsErrors) e.severity = C4Compiler::ErrorSeverity::Error;
				kept.Push(e);
			}
			m_Result.errors = kept;
		}

		// --- Phase 3: backend -------------------------------------------------------------

		// The located diagnostics, and the backend's own message when none of them is an error (a warning beside a
		// missing entry point or an invalid module must not hide why the build failed).
		void ReportBackendFailure(const C4Compiler::ProgramBackend& driver, const std::string& backendError)
		{
			bool located = false;
			for (const C4Compiler::CompilationError& e : driver.LoweringErrors())
			{
				m_Result.errors.Push(e);
				located = located || e.severity != C4Compiler::ErrorSeverity::Warning;
			}
			if (!located) m_Result.AddBackendError(Core::String(backendError.c_str()));
		}

		// The entry point the generated main calls: a -test run drives the test harness (the compiler emits a synthetic main
		// that runs every [test] function), otherwise the user's main. Null when it is not a -test build.
		const C4Compiler::TestHarnessOptions* Tests()
		{
			if (!m_Options.runTests) return nullptr;
			m_Tests.jsonPath = m_Options.testJsonPath.c_str();
			m_Tests.fileOf = [this](uint32 sourceId) { return m_Compiler.GetSourceManager().GetInfo(sourceId).displayName; };
			if (!m_Options.projectsFile.empty()) m_Tests.ownUnit = [this](uint32 unit) { return InRootProject(unit); };
			return &m_Tests;
		}

		bool RunBackend()
		{
			m_Context = std::make_unique<llvm::LLVMContext>();
			m_Backend = std::make_unique<C4Compiler::ProgramBackend>(*m_Context, m_Options.target.System());
			C4Compiler::ProgramBackend& driver = *m_Backend;

			std::string backendError;
			if (m_Options.debugInfo) driver.EnableDebugInfo();
			if (!m_Options.strictAliasing) driver.DisableStrictAliasing();
			if (m_Options.stackMaps) driver.UseStackMaps();
			if (m_Options.scriptHost) driver.ShareVtables();
			if (Serving()) driver.KeepPartitionFacts();
			if (ProgramOutput::Links(m_Options)) driver.VerifyLater();
			if (const unsigned count = EmittedPartitions(); count > 1) driver.SplitInto(count, CopiesBetweenPartitions());
			driver.SetReflectionTag(m_Options.reflectTag);
			driver.LowerOn(m_Options.threads);
			auto phaseStart = std::chrono::steady_clock::now();
			bool ok = driver.Lower(m_Compiler.GetProgramModelForLowering(), "main", &backendError, Tests());
			m_Compiler.AddPhaseTime("lower", SecondsSince(phaseStart));
			if (m_Options.timePhases) ReportLowering(driver.LoweringStatistics(), driver.StepSeconds());
			return WriteProgram(ok, backendError);
		}

		// --serve: the program lowered again (BuildEdit) emitted again into a context of its own; the emitter and context
		// before are freed on a thread of their own while the program is written.
		bool EmitAgain()
		{
			std::vector<C4Compiler::RetiredEmission> retired;
			std::string backendError;
			if (!m_Served.ObjectsExist(m_Backend->PartitionCount())) m_Backend->ForgetPartitionFacts(); // a kept object is gone: emit every partition
			const auto phaseStart = std::chrono::steady_clock::now();
			const bool ok = m_Backend->EmitAgain(m_Compiler.GetProgramModelForLowering(), m_Context, retired, &backendError, Tests());
			m_Compiler.AddPhaseTime("emit", SecondsSince(phaseStart));
			std::thread([emissions = std::move(retired)]() mutable { emissions.clear(); }).detach();
			m_Served.emitted.clear();
			for (unsigned i = 0; i < m_Backend->PartitionCount(); ++i) m_Served.emitted.push_back(m_Backend->Emitted(i));
			return WriteProgram(ok, backendError);
		}

		bool Serving() const { return m_Options.serve || !m_Options.serveOn.empty(); }

		// After the backend lowered (`lowered`, else `backendError` says why) and emitted: its diagnostics, then the program.
		bool WriteProgram(bool lowered, const std::string& backendError)
		{
			C4Compiler::ProgramBackend& driver = *m_Backend;
			if (!lowered)
			{
				ReportBackendFailure(driver, backendError);
				return false;
			}
			if (m_Options.gcStats) driver.InstallGcStatsPrinter();
			for (const C4Compiler::CompilationError& e : driver.LoweringErrors())
				m_Result.errors.Push(e); // a successful lowering reports warnings only
			ApplyWarningOptions();
			if (m_Result.HasErrors()) return false;
			std::vector<llvm::Module*> partitions;
			for (unsigned i = 0; i < driver.PartitionCount(); ++i) partitions.push_back(&driver.PartitionModule(i));
			const C4Compiler::KeyedModule image{ driver.ReflectionImageKey(),
				[&driver](llvm::LLVMContext& context) { return driver.BuildReflectionModule(context); } };
			return ProgramOutput({ m_Options, m_Result, m_Compiler, m_Interop, m_ExportedMacros, Serving() ? &m_Served : nullptr }).Write(partitions, image);
		}

		// A described build's harness runs the root project's tests; each dependency's own harness runs its (docs/language/15-projects.md rule 7).
		bool InRootProject(uint32 unit)
		{
			const C4Compiler::SourceManager& sources = m_Compiler.GetSourceManager();
			if (!sources.HasInfo(unit)) return false;
			const C4Compiler::SourceInfo& info = sources.GetInfo(unit);
			return !info.library && info.project == sources.Projects().Root();
		}

		// -ftime: the bodies lowered on the threads in passes, and the changes they asked the model for (ParallelLowering).
		static void ReportLowering(const C4Compiler::LoweringStats& stats, const std::vector<std::pair<const char*, double>>& steps)
		{
			const char* lead = "lower: ";
			for (const auto& [step, seconds] : steps)
			{
				std::printf("%s%s %.3f s", lead, step, seconds);
				lead = ", ";
			}
			if (!steps.empty()) std::printf("\n");
			uint64_t requests = 0;
			for (const auto& [what, count] : stats.requests) requests += count;
			std::printf("lowering: %llu bodies in %llu pass(es) (%.3f s), other work on the threads %.3f s, %.3f s settling and ordering, "
				"%llu requests", (unsigned long long)stats.bodies, (unsigned long long)stats.passes, stats.lowerSeconds, stats.otherSeconds,
				stats.prepareSeconds, (unsigned long long)requests);
			const char* separator = " (";
			for (const auto& [what, count] : stats.requests)
			{
				std::printf("%s%s %llu", separator, what.c_str(), (unsigned long long)count);
				separator = ", ";
			}
			std::printf("%s\n", stats.requests.empty() ? "" : ")");
		}

		// --- Phase 1: input registration -------------------------------------------------

		bool RegisterSources()
		{
			uint32 total = 0;
			if (!m_Options.projectsFile.empty())
			{
				const C4Compiler::ProjectFileResult read = C4Compiler::ReadProjectFile(fs::path(m_Options.projectsFile.c_str()));
				C4Compiler::ProjectSetResult checked = read.error.empty() ? C4Compiler::ProjectSet::Check(read.list) : C4Compiler::ProjectSetResult{};
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

				if (!C4Compiler::FileExists(C4Compiler::PathText(input)))
				{
					std::cerr << "error: Input file/folder not found: " << input << std::endl;
					return false;
				}

				if (C4Compiler::IsFolder(C4Compiler::PathText(input)))
				{
					total += m_Compiler.RegisterFolder(input);
					CORE_INFO("Registered source file(s) from folder {0}", input.string());
					continue;
				}

				// A single file is registered together with its folder so that the files it
				// includes can be resolved.
				fs::path parentDir = fs::absolute(input).parent_path();
				if (!parentDir.empty() && C4Compiler::IsFolder(C4Compiler::PathText(parentDir)))
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
			const fs::path bedrockDir = C4Compiler::Toolchain::Bedrock();
			if (!C4Compiler::IsFolder(C4Compiler::PathText(bedrockDir)))
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

		// Every project's source folders and files (docs/language/15-projects.md); a source folder's tests/ only in a -test
		// build (rule 7), so what a project's tests use is never part of its programs.
		uint32 RegisterProjectSources()
		{
			uint32 total = 0;
			const C4Compiler::FolderScan scan = m_Options.runTests ? C4Compiler::FolderScan::Recursive : C4Compiler::FolderScan::WithoutTests;
			const C4Compiler::ProjectSet& projects = m_Compiler.GetSourceManager().Projects();
			for (uint32 p = 0; p < projects.Count(); ++p)
				for (const fs::path& source : projects.At(p).sources)
				{
					if (C4Compiler::IsFolder(C4Compiler::PathText(source))) total += m_Compiler.RegisterFolder(source, scan);
					else if (m_Compiler.RegisterFile(source)) ++total;
					else std::cerr << "warning: project '" << projects.At(p).name << "' names '" << source.generic_string() << "', which does not exist\n";
				}
			CORE_INFO("Registered the sources of {0} project(s)", projects.Count());
			return total;
		}

		// --- Phase 2: preprocess (macro expansion + interop) ------------------------------

		void RunPreprocessPhase()
		{
			// The macro pass reads every registered source - the default library's too - each starting with its project's
			// macros (D-59), and lays what it does over the file's own text (R8): each source keeps its text, and its parse
			// reads it through its MacroLayout.
			auto& sources = m_Compiler.GetSourceManager();
			auto macroResults = C4Compiler::ExpandSourceMacros(sources, C4Compiler::PredefinedMacros(m_Options.runTests, m_Options.target, m_Options.defines));
			m_Compiler.AddMacroDiagnostics(macroResults);
			for (const auto& result : macroResults) m_ExportedMacros[result.absPath] = result.exported;
			C4Compiler::ApplyMacroLayouts(sources, macroResults);

			RunInteropPrepass(ResolveRootFolders());
			C4Compiler::RegisterBindings(sources, m_Interop.files);
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
				if (C4Compiler::IsFolder(C4Compiler::PathText(input)))
				{
					roots.push_back(input.string());
					continue;
				}

				fs::path parentDir = fs::absolute(input).parent_path();
				if (!parentDir.empty() && C4Compiler::IsFolder(C4Compiler::PathText(parentDir)))
					roots.push_back(parentDir.string());
			}

			return roots;
		}

		/// Interop pre-pass: generate C4 bindings for `extern "c" include` headers - each a unit of its own the including
		/// file includes (R8) - and compile `extern "c" source` files into objects to link. It reads each file as its
		/// parse will.
		void RunInteropPrepass(const std::vector<std::string>& rootFolders)
		{
			std::vector<std::string> cliIncludes;
			for (const auto& inc : m_Options.includeDirs)
				cliIncludes.push_back(std::string(inc.c_str()));

			// The build's own folder: two programs built at once never share a foreign object. Absolute, as CBind and clang
			// run in other working folders (BUG-318).
			const fs::path interopFolder = fs::absolute(C4Compiler::IntermediateFolder(m_Options) / (fs::path(m_Options.outputFile.c_str()).stem().string() + ".interop"));
			const std::error_code ec = C4Compiler::CreateFolders(interopFolder.string());
			if (ec)
			{
				m_Result.AddBackendError("cannot create the interop folder " + Core::String(interopFolder.string().c_str()) + ": " + Core::String(ec.message().c_str()));
				return;
			}
			const std::string objDir = interopFolder.string();

			// Foreign sources build at the program's optimization level, with its debug information (BUG-285).
			std::vector<std::string> foreignFlags = { "-O" + std::to_string(m_Options.optLevel) };
			if (m_Options.debugInfo) foreignFlags.push_back("-g");
			// A file's headers are found in its project's folders and its dependencies' (D-59).
			const auto inputs = C4Compiler::InteropInputsOf(m_Compiler.GetSourceManager(), !m_Options.projectsFile.empty());
			m_Interop = C4Compiler::InteropPass::RunFolder(rootFolders, inputs, cliIncludes, objDir, m_Options.target, m_Options.verbose,
				false, foreignFlags);

			// A broken foreign C/C++ file (unresolvable path, CBind rejection, failed compile) must fail
			// the build rather than silently produce a binary missing its declarations/objects. The exact
			// Clang diagnostics were already printed by the interop pass; these hard errors gate the build.
			for (const auto& e : m_Interop.errors)
				m_Result.AddBackendError(e);
		}

		void ExportMacrosIfRequested(const std::vector<C4Compiler::MacroPass::FileResult>& macroResults)
		{
			if (!m_Options.exportMacros)
				return;

			std::string outDir = m_Options.outputFile;
			size_t slash = outDir.find_last_of("/\\");
			outDir = (slash != std::string::npos) ? outDir.substr(0, slash) : ".";
			C4Compiler::MacroPass::ExportTables(macroResults, outDir);
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

				C4Compiler::PrettyPrintConsumer consumer;
				C4Compiler::ASTWalker::Walk(rootAST, consumer);
			}
		}

		C4Compiler::CompileOptions m_Options;
		C4Compiler::CompilerInstance m_Compiler;
		C4Compiler::CompilationResult m_Result;
		C4Compiler::InteropPass::Result m_Interop;
		std::string m_LibraryFolder; // the default library (Bedrock) next to C4.exe, expanded with the inputs
		std::unordered_map<std::string, C4Compiler::MacroTable> m_ExportedMacros;
		// The backend's context and driver (the lowered HIR, the modules), freed with the driver: a one-shot build never
		// frees its driver (main), since freeing them object by object takes a second on a large program (clang's
		// -disable-free does the same); a server frees each between two requests. The driver goes before its context.
		std::unique_ptr<llvm::LLVMContext> m_Context;
		std::unique_ptr<C4Compiler::ProgramBackend> m_Backend;
		C4Compiler::TestHarnessOptions m_Tests;
		C4Compiler::RetiredParses m_Retired; // the parses edits replaced, which the model still points into
		ServedOutputs m_Served;              // --serve: each partition's output, for the builds after
		int m_Code = 1;                      // the last build's exit code // by absolute path: what each file exports
	};
}

#ifdef _WIN32
// The faulting thread's frames from the fault's own context, outward, by each function's unwind data (the filter runs
// on top of the exception's dispatch, where a capture of the current stack finds nothing of the fault, BUG-394).
static void ReportFaultingStack(CONTEXT context, uintptr_t base)
{
	constexpr int kMostFrames = 48;
	for (int i = 0; i < kMostFrames && context.Rip != 0; ++i)
	{
		fprintf(stderr, "  C4.exe+0x%llx\n", (unsigned long long)(context.Rip - base));
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
		if (!function)
		{
			context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp); // a leaf: its return address is on top
			context.Rsp += sizeof(DWORD64);
			continue;
		}
		void* handlerData = nullptr;
		DWORD64 establisher = 0;
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
	}
}

// A crash of the compiler itself is reported as one, with the faulting offset inside C4.exe and the stack's, which
// llvm-symbolizer turns into source lines against this build's C4.pdb.
static LONG WINAPI ReportInternalError(EXCEPTION_POINTERS* ep)
{
	const auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
	fprintf(stderr, "C4: internal compiler error: exception 0x%lx at C4.exe+0x%llx\n", ep->ExceptionRecord->ExceptionCode,
		(unsigned long long)(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) - base));
	ReportFaultingStack(*ep->ContextRecord, base);
	fprintf(stderr, "Please report it with the program that triggered it.\n");
	return EXCEPTION_EXECUTE_HANDLER;
}

static void InstallCrashReport(const char*)
{
	SetUnhandledExceptionFilter(ReportInternalError);
}

// The process ends without its libraries' shutdown.
static void EndProcess(int code)
{
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
}
#else
static void ReportInternalError(void*)
{
	fprintf(stderr, "C4: internal compiler error: the compiler crashed; its stack:\n");
}

static void AskForReport(void*)
{
	fprintf(stderr, "Please report it with the program that triggered it.\n");
}

// A crash of the compiler itself is reported as one, with the faulting thread's stack by LLVM's signal handler
// (symbolized when llvm-symbolizer is on PATH), between these two lines.
static void InstallCrashReport(const char* program)
{
	llvm::sys::AddSignalHandler(ReportInternalError, nullptr);
	llvm::sys::PrintStackTraceOnErrorSignal(program);
	llvm::sys::AddSignalHandler(AskForReport, nullptr);
}

// The process ends without its libraries' shutdown: no static destructors, no atexit handlers.
static void EndProcess(int code)
{
	std::_Exit(code);
}
#endif

// The exit code of a command line C4.exe cannot run: bad options or no input.
constexpr int kUsageError = 2;

// A request line of --serve, without the carriage return and spaces a Windows pipe may leave after it.
static std::string_view RequestOf(const std::string& line)
{
	std::string_view request = line;
	while (!request.empty() && (request.back() == '\r' || request.back() == ' ' || request.back() == '\t')) request.remove_suffix(1);
	return request;
}

// --serve (docs/compiler/07 stages 2a, 2b): a build per 'build' line on stdin, each answered by 'done <exit code>' after
// its diagnostics. The build before stays: its sources' parses are the next build's, and when the edit between them
// changed only bodies their own functions read and globals' values, it takes the edit (CompilerDriver::BuildEdit) instead
// of a cold build. What a
// build no longer needs is freed once it has answered, while the client waits for its next edit.
// One build of a server: `live` (the build before, if any) takes the edit when it can, else a cold build of the sources
// as they are now becomes `live`. Its exit code; what `spent` gets is no longer needed, to be freed after answering.
static int ServeBuild(const C4Compiler::CompileOptions& opts, std::unique_ptr<CompilerDriver>& live, std::unique_ptr<CompilerDriver>& spent)
{
	auto edited = std::make_unique<CompilerDriver>(opts, live ? live->Parses() : nullptr);
	if (!edited->Prepare())
	{
		spent = std::move(edited);
		return 1;
	}
	if (const std::optional<int> served = live ? live->BuildEdit(*edited) : std::nullopt)
	{
		spent = std::move(edited);
		return *served;
	}
	const int code = edited->Finish();
	spent = std::exchange(live, std::move(edited));
	return code;
}

static int Serve(const C4Compiler::CompileOptions& opts)
{
	std::unique_ptr<CompilerDriver> live;
	std::string line;
	while (std::getline(std::cin, line))
	{
		const std::string_view request = RequestOf(line);
		if (request == "quit") break;
		if (request != "build" && !request.starts_with("build "))
		{
			std::cout << "error: unknown request '" << request << "': give 'build' or 'quit'" << std::endl;
			continue;
		}
		std::unique_ptr<CompilerDriver> spent;
		const int code = ServeBuild(opts, live, spent);
		std::cerr.flush();
		std::cout << "done " << code << std::endl;
	}
	return 0;
}

// --serve-on (docs/compiler/07, Nuke's `server` setting): the same builds for the clients of a pipe, one at a time, each
// asking 'build <stamp>'; a stamp other than the server's (its compiler was rebuilt) is answered 'stale' and ends it, and
// so does --serve-idle without a client, and 'quit' (answered 'bye': nuke clean). A second server of the name ends at
// once: the first serves.
static int ServeOn(const C4Compiler::CompileOptions& opts)
{
	std::string error;
	const std::unique_ptr<ServeChannel> channel = ServeChannel::Listen(opts.serveOn, error);
	if (!channel)
	{
		if (!error.empty()) std::cerr << "error: " << error << std::endl;
		return error.empty() ? 0 : 1;
	}
	std::unique_ptr<CompilerDriver> live;
	while (channel->Accept(opts.serveIdleSeconds))
	{
		const std::optional<std::string> line = channel->ReadLine();
		const std::string_view request = line ? RequestOf(*line) : std::string_view();
		if (request == "quit")
		{
			channel->WriteLine("bye");
			channel->Hangup();
			break;
		}
		if (request != "build " + opts.serveStamp && request != "build")
		{
			channel->WriteLine(request.starts_with("build") ? "stale" : "error: unknown request '" + std::string(request) + "': give 'build <stamp>' or 'quit'");
			channel->Hangup();
			if (request.starts_with("build")) break;
			continue;
		}
		std::unique_ptr<CompilerDriver> spent;
		int code = 1;
		{
			const ServeChannel::OutputToClient output(*channel);
			code = ServeBuild(opts, live, spent);
		}
		channel->WriteLine("done " + std::to_string(code));
		channel->Hangup();
	}
	return 0;
}

int main(int argc, char** argv)
{
	InstallCrashReport(argv[0]);

	const C4Compiler::CompileOptions opts = C4Compiler::ParseArgs(argc, argv);
	if (!opts.error.empty())
	{
		std::cerr << "error: " << opts.error << std::endl;
		return kUsageError;
	}
	if (opts.printHelp)
	{
		C4Compiler::PrintUsage();
		return 0;
	}
	if (opts.printVersion)
	{
		std::cout << "C4 " << C4Compiler::CompilerVersion::Major << "." << C4Compiler::CompilerVersion::Minor << "." << C4Compiler::CompilerVersion::Patch << std::endl;
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
	const int code = opts.serve ? Serve(opts) : !opts.serveOn.empty() ? ServeOn(opts)
		: (new CompilerDriver(opts, nullptr))->Run(); // the program, its model and its backend go with the process
	// Everything is written: the process ends without its libraries' shutdown, which hands each of the allocator's
	// spans back one by one (a tenth of a build that changed nothing) for memory the system takes back whole.
	std::cout.flush();
	std::cerr.flush();
	std::fflush(nullptr);
	EndProcess(code);
	return code;
}
