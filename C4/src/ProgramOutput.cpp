#include "ProgramOutput.h"
#include "Core/Core.h"
#include "Compiler/ObjectCache.h"
#include "Compiler/ObjectEmission.h"
#include "Compiler/Version.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/Toolchain.h"
#include "Backend/CodeGen/AsmEmitter.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Script/HostInterface.h"
#include "Script/HostTable.h"
#include "Script/InlineBodies.h"
#include "Script/InterfaceBuilder.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>
#include <optional>
#include <system_error>

namespace fs = std::filesystem;

bool ProgramOutput::Write(llvm::Module& module)
{
	if (!MakeOutputFolder()) return false;
	const std::string base = IntermediateBase();
	if (base.empty()) return false;
	std::optional<Scripting::HostInterface> host;
	if (!PrepareScriptHost(module, host)) return false;
	if (!WriteProgram(module, base)) return false;
	return !host || WriteScriptInterface(*host); // only beside the program it describes: a failed link keeps the old pair
}

bool ProgramOutput::WriteProgram(llvm::Module& module, const std::string& base)
{
	if (m_In.options.emitBitcode) return WriteBitcode(module, base + ".bc");
	if (m_In.options.emitIR) return WriteIR(module, base + ".ll"); // -S: the module as text, and nothing else
	const bool optimizePartitions = m_In.options.incremental && m_In.options.optLevel > 0; // -O0 optimizes nothing to cache
	const Scripting::ObjectSettings settings{ m_In.options.optLevel, &m_In.options.target, m_In.options.threads, optimizePartitions };
	if (!optimizePartitions && !Optimize(module, settings)) return false;
	if (m_In.options.compileOnly) return WriteCompiledObject(module, settings, base + ".obj"); // -c: the object is the output
	const std::vector<std::string> objects = Assemble(module, settings, Scripting::ObjectCache(base + ".objects", CacheSalt(settings)));
	return !objects.empty() && Link(objects);
}

// The compiler owns its output location: a missing folder is created, as every toolchain does.
bool ProgramOutput::MakeOutputFolder()
{
	fs::path folder = fs::path(m_In.options.outputFile.c_str()).parent_path();
	if (folder.empty()) folder = fs::current_path();
	std::error_code ec;
	fs::create_directories(folder, ec);
	if (!ec) return true;
	m_In.result.AddBackendError("failed to create output folder: " + Core::String(folder.string().c_str()));
	return false;
}

// Where the intermediates go: `--int-dir`'s folder with the output's name, else next to the output, as `<path>/<name>`
// without an extension. Empty (with the error recorded) when the folder cannot be made.
std::string ProgramOutput::IntermediateBase()
{
	const fs::path folder = Scripting::IntermediateFolder(m_In.options);
	std::error_code ec;
	if (!folder.empty()) fs::create_directories(folder, ec);
	if (ec)
	{
		m_In.result.AddBackendError("failed to create the intermediate folder: " + Core::String(folder.string().c_str()));
		return "";
	}
	return (folder / fs::path(m_In.options.outputFile.c_str()).stem()).string();
}

bool ProgramOutput::WriteIR(const llvm::Module& module, const std::string& path)
{
	std::error_code ec;
	llvm::raw_fd_ostream file(path, ec);
	if (ec)
	{
		m_In.result.AddBackendError("failed to open IR output: " + Core::String(path.c_str()));
		return false;
	}
	module.print(file, nullptr);
	CORE_INFO("Emitted LLVM IR to {0}", path);
	return true;
}

// -bc: the module as bitcode, the input of the scripting pipeline.
bool ProgramOutput::WriteBitcode(const llvm::Module& module, const std::string& path)
{
	std::error_code ec;
	llvm::raw_fd_ostream file(path, ec);
	if (ec)
	{
		m_In.result.AddBackendError("failed to open the bitcode output: " + Core::String(path.c_str()));
		return false;
	}
	llvm::WriteBitcodeToFile(module, file);
	CORE_INFO("Wrote bitcode to {0}", path);
	return true;
}

bool ProgramOutput::Optimize(llvm::Module& module, const Scripting::ObjectSettings& settings)
{
	const auto start = std::chrono::steady_clock::now();
	if (m_In.options.verbose) std::cout << "Optimizing (-O" << settings.optLevel << ")" << std::endl;
	std::vector<Scripting::ObjectDiagnostic> diagnostics;
	const bool optimized = Scripting::OptimizeModule(module, settings, diagnostics);
	Report(diagnostics);
	Timed("optimize", start);
	return optimized;
}

// --partitions, or as many as the module's size is worth.
unsigned ProgramOutput::PartitionsFor(const llvm::Module& module, const Scripting::ObjectSettings& settings) const
{
	return m_In.options.partitions ? m_In.options.partitions : Scripting::PartitionCount(module, settings);
}

// What a cached object depends on besides its code: the compiler that assembled it (its version, and the size and
// time of its executable, so a rebuilt compiler never reuses an old one's objects) and the target settings.
std::string ProgramOutput::CacheSalt(const Scripting::ObjectSettings& settings)
{
	std::error_code ec;
	const fs::path compiler = Scripting::Toolchain::Executable();
	const auto size = fs::file_size(compiler, ec);
	const auto time = fs::last_write_time(compiler, ec).time_since_epoch().count();
	return "C4 " + std::to_string(Scripting::CompilerVersion::Encoded) + " " + std::to_string(size) + " " + std::to_string(time) +
		" -O" + std::to_string(settings.optLevel) + (settings.optimizePartitions ? " incremental " : " ") + settings.target->Cpu() + " " +
		settings.target->LlvmFeatures();
}

bool ProgramOutput::WriteCompiledObject(llvm::Module& module, const Scripting::ObjectSettings& settings, const std::string& objPath)
{
	const auto start = std::chrono::steady_clock::now();
	std::vector<Scripting::ObjectDiagnostic> diagnostics;
	const bool written = Scripting::WriteObject(module, settings, objPath, diagnostics);
	Report(diagnostics);
	Timed("codegen", start);
	if (written) CORE_INFO("Compiled {0}", objPath);
	return written;
}

// The module assembled into its objects inside the compiler, reusing the cached objects of unchanged partitions; an
// error the assembler reports for an asm block is placed at that block.
std::vector<std::string> ProgramOutput::Assemble(llvm::Module& module, const Scripting::ObjectSettings& settings, const Scripting::ObjectCache& cache)
{
	const auto start = std::chrono::steady_clock::now();
	const unsigned partitions = PartitionsFor(module, settings);
	std::vector<Scripting::ObjectDiagnostic> diagnostics;
	Scripting::CachedObjects objects = Scripting::WriteCachedObjects(module, settings, partitions, cache, diagnostics);
	Report(diagnostics);
	Timed(settings.optimizePartitions ? "optimize+codegen" : "codegen", start);
	if (m_In.options.verbose)
		std::cout << Scripting::InstructionCount(module) << " instructions in " << partitions << " object(s), " << objects.reused
			<< " reused, the rest assembled on up to " << settings.threads << " thread(s), in " << cache.Folder().string() << std::endl;
	return objects.paths;
}

void ProgramOutput::Report(const std::vector<Scripting::ObjectDiagnostic>& diagnostics)
{
	for (const Scripting::ObjectDiagnostic& diagnostic : diagnostics)
	{
		Scripting::CompilationError error;
		if (diagnostic.cookie) error.location = Scripting::AsmEmitter::LocationOf(diagnostic.cookie);
		error.message = Core::String(diagnostic.message.c_str());
		m_In.result.errors.Push(error);
	}
}

bool ProgramOutput::Link(const std::vector<std::string>& programObjects)
{
	const auto start = std::chrono::steady_clock::now();
	const Core::String& out = m_In.options.outputFile;
	Core::Vector<Core::String> objects;
	for (const std::string& object : programObjects) objects.Push(Core::String(object.c_str()));
	// The C4 runtime linked into every executable (Compiler/RuntimeObjects.h).
	if (!Scripting::EnsureRuntimeObjects(m_In.options.verbose, objects))
	{
		m_In.result.AddBackendError("the C4 runtime could not be built");
		return false;
	}
	// Foreign C/C++ objects compiled by the interop pre-pass (`extern "c" source` files and their shims): a [link]
	// variable or extern function only resolves against them.
	for (const auto& foreign : m_In.interop.objects)
		if (!foreign.objPath.empty()) objects.Push(Core::String(foreign.objPath.c_str()));
	Core::Vector<Core::String> libraries;
	for (const auto& lib : m_In.interop.libraries) libraries.Push(Core::String(lib.c_str()));
	for (const auto& argument : m_In.options.linkArguments) libraries.Push(argument);
	const DebugInfoPaths debugInfo = DebugInfoPathsFor(out);
	if (m_In.options.debugInfo)
	{
		// Linked under another name, so a link that fails keeps the old program's .pdb; the program names the final one.
		libraries.Push("-Wl,/DEBUG");
		libraries.Push(Core::String(("-Wl,/PDB:" + debugInfo.partial.string()).c_str()));
		libraries.Push(Core::String(("-Wl,/PDBALTPATH:" + debugInfo.final.string()).c_str()));
	}
	const fs::path scriptHost = m_In.options.scriptHost ? Scripting::Toolchain::ScriptHost() : fs::path();
	if (m_In.options.scriptHost)
	{
		if (scriptHost.empty())
		{
			m_In.result.AddBackendError("--script-host needs the script host library, C4Script.dll, which this install does not have");
			return false;
		}
		libraries.Push(Core::String((scriptHost / "C4Script.lib").string().c_str()));
		// Loaded on the first call, so a program without the DLL beside it starts and Host::Open says so.
		libraries.Push("-Wl,/DELAYLOAD:C4Script.dll");
		libraries.Push("-ldelayimp");
	}
	if (!Scripting::LinkWithLLD(objects, libraries, out, m_In.options.subsystem, m_In.options.verbose))
	{
		std::error_code ec;
		fs::remove(debugInfo.partial, ec);
		m_In.result.AddBackendError("failed to link " + Core::String(out.c_str()));
		return false;
	}
	CORE_INFO("Linked {0}", out);
	if (m_In.options.debugInfo && !PlaceDebugInfo(debugInfo)) return false;
	if (m_In.options.scriptHost && !ShipScriptHost(scriptHost)) return false;
	Timed("runtime+link", start);
	return true;
}

ProgramOutput::DebugInfoPaths ProgramOutput::DebugInfoPathsFor(const Core::String& program)
{
	const fs::path final = fs::absolute(fs::path(program.c_str())).replace_extension(".pdb");
	return { final, fs::path(final.string() + ".partial") };
}

// The new .pdb over the old one, now that the program it describes is in place.
bool ProgramOutput::PlaceDebugInfo(const DebugInfoPaths& paths)
{
	std::error_code ec;
	fs::rename(paths.partial, paths.final, ec);
	if (!ec) return true;
	fs::remove(paths.partial, ec);
	m_In.result.AddBackendError(Core::String(("cannot replace " + paths.final.string() + " (is a debugger holding it?)").c_str()));
	return false;
}

// --script-host (docs/runtime/04 section 3): the thread_local accessors, the symbol table, and the interface `host` that
// goes beside the program once it is built. A program that uses the scripting API without it has no table to link
// against: an error that says what to do.
bool ProgramOutput::PrepareScriptHost(llvm::Module& module, std::optional<Scripting::HostInterface>& host)
{
	if (!m_In.options.scriptHost)
	{
		if (!module.getNamedGlobal("__c4_script_table")) return true;
		m_In.result.AddBackendError("this program uses the scripting API (Scripting::Host), which needs a script host: build it with --script-host");
		return false;
	}
	Scripting::EmitThreadLocalAccessors(module);
	const Scripting::ProgramModel& model = m_In.compiler.GetProgramModel();
	std::string error;
	std::vector<std::string> objects;
	for (const auto& foreign : m_In.interop.objects)
		if (!foreign.objPath.empty()) objects.push_back(foreign.objPath);
	std::optional<std::vector<std::string>> listed = Scripting::ForeignDefinitions(objects, &error);
	if (!listed)
	{
		m_In.result.AddBackendError(Core::String(error.c_str()));
		return false;
	}
	const std::vector<std::string> prelude = Scripting::PreludeFunctionSymbols(model);
	listed->insert(listed->begin(), prelude.begin(), prelude.end());
	Scripting::HostBuild build;
	build.symbols = Scripting::HostSymbols(module, *listed);
	build.inlineBodies = Scripting::InlineBodies(module, build.symbols);
	build.reflectedTypes = Scripting::ReflectedTypeNames(model);
	build.debugInfo = m_In.options.debugInfo;
	host = Scripting::BuildInterface(m_In.compiler, m_In.exportedMacros, std::move(build), &error);
	if (!host || !Scripting::EmitHostTable(module, host->symbols, host->buildId, &error))
	{
		m_In.result.AddBackendError(Core::String(error.c_str()));
		return false;
	}
	return true;
}

bool ProgramOutput::WriteScriptInterface(const Scripting::HostInterface& host)
{
	const std::string interfacePath = fs::path(m_In.options.outputFile.c_str()).replace_extension(".c4i").string();
	std::string error;
	if (!Scripting::WriteInterface(host, interfacePath, &error))
	{
		m_In.result.AddBackendError(Core::String(error.c_str()));
		return false;
	}
	CORE_INFO("Wrote the script interface to {0}", interfacePath);
	return true;
}

// The script host library goes beside the program, which loads it as it starts.
bool ProgramOutput::ShipScriptHost(const fs::path& scriptHost)
{
	const fs::path target = fs::path(m_In.options.outputFile.c_str()).parent_path() / "C4Script.dll";
	std::error_code ec;
	fs::copy_file(scriptHost / "C4Script.dll", target, fs::copy_options::update_existing, ec);
	if (!ec) return true;
	m_In.result.AddBackendError(Core::String(("cannot copy C4Script.dll beside the program: " + ec.message()).c_str()));
	return false;
}

void ProgramOutput::Timed(const char* phase, std::chrono::steady_clock::time_point since)
{
	m_In.compiler.AddPhaseTime(phase, std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count());
}
