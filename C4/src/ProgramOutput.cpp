#include "ProgramOutput.h"
#include "Core/Core.h"
#include "Compiler/FileIo.h"
#include "Compiler/IrVerification.h"
#include "Compiler/ModulePartitions.h"
#include "Compiler/ObjectCache.h"
#include "Compiler/ObjectEmission.h"
#include "Compiler/ParallelFor.h"
#include "Compiler/PartitionMerge.h"
#include "Compiler/Version.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/Toolchain.h"
#include "Backend/CodeGen/AsmEmitter.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Script/HostInterface.h"
#include "Script/HostTable.h"
#include "Script/InlineBodies.h"
#include "Script/InlineBodyCache.h"
#include "Script/InterfaceBuilder.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
	// The script host library's file, as Nuke's C/C++ driver names the shared libraries it builds for the target.
	std::string ScriptHostLibrary(const Scripting::TargetAbi& target) { return target.SharedLibrary("C4Script"); }
}

bool ProgramOutput::Write(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image)
{
	if (!MakeOutputFolder()) return false;
	const std::string base = IntermediateBase();
	if (base.empty()) return false;
	if (!PrepareScriptHost(partitions)) return false;
	const bool written = !Links(m_In.options) ? WriteModule(*partitions.front(), base)
		: m_In.options.incremental ? WritePartitions(partitions, image, base) : WriteWholeProgram(partitions, image, base);
	return written && (!m_Interface || WriteScriptInterface()); // only beside the program it describes: a failed link keeps the old pair
}

bool ProgramOutput::Links(const Scripting::CompileOptions& options)
{
	return !options.emitIR && !options.emitBitcode && !options.compileOnly;
}

// -S, -bc, -c: the program's one module as text, bitcode or one object, and nothing else.
bool ProgramOutput::WriteModule(llvm::Module& module, const std::string& base)
{
	if (m_In.options.emitBitcode) return WriteBitcode(module, base + ".bc");
	if (m_In.options.emitIR) return WriteIR(module, base + ".ll");
	const Scripting::ObjectSettings settings{ m_In.options.optLevel, &m_In.options.target, m_In.options.threads };
	return Optimize(module, settings) && WriteCompiledObject(module, settings, base + ".obj");
}

// A whole-program build (Dist): the partitions merged into one module (R12), which is optimized whole, then split again
// for code generation on the output's threads, its objects kept by their code, then the link. A program of one partition
// is its module already.
bool ProgramOutput::WriteWholeProgram(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image, const std::string& base)
{
	const Scripting::ObjectSettings settings{ m_In.options.optLevel, &m_In.options.target, m_In.options.threads };
	std::unique_ptr<llvm::Module> merged;
	if (partitions.size() > 1 && !(merged = Merged(partitions, image, base, settings))) return false;
	llvm::Module& program = merged ? *merged : *partitions.front();
	if (!Verified(program) || !Optimize(program, settings)) return false;
	std::vector<std::string> objects = Assemble(program, settings, Scripting::ObjectCache(base + ".objects", CacheSalt(settings)));
	if (objects.empty()) return false;
	if (merged && m_HostBuild && !FinishScriptHost(program, settings, base, objects)) return false;
	return Link(objects, base);
}

// The partitions' bitcode, written on the output's threads, gives a script host its inline bodies (from the inline body
// cache, as an incremental build's do) and is read back into one module with the reflection image and the script table
// (PartitionMerge). Its context is never freed, like the backend's: the process ends with the build.
std::unique_ptr<llvm::Module> ProgramOutput::Merged(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image,
	const std::string& base, const Scripting::ObjectSettings& settings)
{
	auto start = std::chrono::steady_clock::now();
	std::vector<std::string> bitcode(partitions.size());
	Scripting::ParallelFor(partitions.size(), settings.threads, [&](size_t i) {
		bitcode[i] = Scripting::PartitionBitcode(*partitions[i], Scripting::PartitionOptimization::Whole);
	});
	Timed("bitcode", start);
	if (m_HostBuild && !CollectInlineBodies(partitions, bitcode, base, settings)) return nullptr;
	start = std::chrono::steady_clock::now();
	std::vector<Scripting::KeyedModule::Builder> beside;
	if (!image.key.empty()) beside.push_back(image.build);
	if (m_HostBuild) beside.push_back(HostTable(*partitions.front()).build);
	std::string error;
	std::unique_ptr<llvm::Module> merged = Scripting::MergePartitions(*new llvm::LLVMContext, bitcode, beside, error);
	if (!merged) m_In.result.AddBackendError(Core::String(error.c_str()));
	Timed("merge", start);
	return merged;
}

bool ProgramOutput::Verified(const llvm::Module& module)
{
	const std::optional<std::string> invalid = Scripting::InvalidIr(module);
	if (invalid) m_In.result.AddBackendError(Core::String(invalid->c_str()));
	return !invalid;
}

// An incremental build (D-76): each partition optimized alone, its object reused or assembled on the thread that wrote its
// bitcode (an edit's partitions start while the others' bitcode is written), the inline bodies, then the link.
bool ProgramOutput::WritePartitions(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image, const std::string& base)
{
	const Scripting::ObjectSettings settings{ m_In.options.optLevel, &m_In.options.target, m_In.options.threads, true, true };
	const auto start = std::chrono::steady_clock::now();
	const bool split = partitions.size() > 1;
	// One partition is the whole program: no other one needs its linkonce definitions kept.
	const Scripting::PartitionOptimization optimization = split ? Scripting::PartitionOptimization::Alone : Scripting::PartitionOptimization::Whole;
	std::vector<Scripting::ObjectDiagnostic> diagnostics;
	const Scripting::ObjectCache cache(base + ".objects", CacheSalt(settings));
	std::vector<Scripting::KeyedModule> keyed;
	if (!image.key.empty()) keyed.push_back(image);
	if (m_HostBuild && split) keyed.push_back(HostTable(*partitions.front()));
	const unsigned count = static_cast<unsigned>(partitions.size() + keyed.size());
	std::vector<std::string> bitcode;
	Scripting::CachedObjects objects = Scripting::WriteCachedObjects(partitions.size(),
		[&](size_t i) { return Scripting::PartitionBitcode(*partitions[i], optimization); }, std::move(keyed), settings, cache, diagnostics, bitcode);
	Report(diagnostics);
	Timed("optimize+codegen", start);
	if (m_In.options.timePhases) ReportPartitions(objects, count, settings.threads);
	if (m_In.options.verbose)
	{
		uint64_t instructions = 0;
		for (const llvm::Module* partition : partitions) instructions += Scripting::InstructionCount(*partition);
		ReportObjects(instructions, count, objects.reused, settings.threads, cache);
	}
	if (objects.paths.empty()) return false;
	if (m_HostBuild && split && !CollectInlineBodies(partitions, bitcode, base, settings)) return false;
	if (m_HostBuild && split && !FinishScriptHost(*partitions.front(), settings, base, objects.paths)) return false;
	return Link(objects.paths, base);
}

// The compiler owns its output location: a missing folder is created, as every toolchain does.
bool ProgramOutput::MakeOutputFolder()
{
	fs::path folder = fs::path(m_In.options.outputFile.c_str()).parent_path();
	if (folder.empty()) folder = fs::current_path();
	if (!Scripting::CreateFolders(folder.string())) return true;
	m_In.result.AddBackendError("failed to create output folder: " + Core::String(folder.string().c_str()));
	return false;
}

// Where the intermediates go: `--int-dir`'s folder with the output's name, else next to the output, as `<path>/<name>`
// without an extension. Empty (with the error recorded) when the folder cannot be made.
std::string ProgramOutput::IntermediateBase()
{
	const fs::path folder = Scripting::IntermediateFolder(m_In.options);
	if (!folder.empty() && Scripting::CreateFolders(folder.string()))
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
	m_In.compiler.AddPhaseTime("split", objects.splitSeconds);
	m_In.compiler.AddPhaseTime("codegen", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() - objects.splitSeconds);
	if (m_In.options.timePhases) ReportPartitions(objects, partitions, settings.threads);
	if (m_In.options.verbose) ReportObjects(Scripting::InstructionCount(module), partitions, objects.reused, settings.threads, cache);
	return objects.paths;
}

// -v: how many objects, how many an earlier build had, and where they are kept.
void ProgramOutput::ReportObjects(uint64_t instructions, unsigned objects, size_t reused, unsigned threads, const Scripting::ObjectCache& cache) const
{
	std::cout << instructions << " instructions in " << objects << " object(s), " << reused << " reused, the rest assembled on up to "
		<< threads << " thread(s), in " << cache.Folder().string() << std::endl;
}

// The partitions assembled, how long the longest took and all of them together: how well the threads were used.
void ProgramOutput::ReportPartitions(const Scripting::CachedObjects& objects, unsigned partitions, unsigned threads) const
{
	double longest = 0, total = 0;
	uint64_t largest = 0;
	for (const Scripting::CachedObjects::Assembled& assembled : objects.assembled)
	{
		longest = std::max(longest, assembled.seconds);
		total += assembled.seconds;
		largest = std::max(largest, assembled.instructions);
	}
	std::printf("partitions: %u (%zu reused), %zu assembled on %u thread(s): longest %.3f s, all %.3f s, largest %llu instructions\n",
		partitions, objects.reused, objects.assembled.size(), threads, longest, total, (unsigned long long)largest);
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

bool ProgramOutput::Link(const std::vector<std::string>& programObjects, const std::string& base)
{
	const auto start = std::chrono::steady_clock::now();
	const Core::String& out = m_In.options.outputFile;
	Core::Vector<Core::String> objects;
	for (const std::string& object : programObjects) objects.Push(Core::String(object.c_str()));
	// The C4 runtime linked into every executable (Compiler/RuntimeObjects.h).
	const Scripting::TargetAbi& target = m_In.options.target.Abi();
	Core::Vector<Core::String> runtime;
	if (!Scripting::EnsureRuntimeObjects(m_In.options.verbose, target, runtime))
	{
		m_In.result.AddBackendError("the C4 runtime could not be built");
		return false;
	}
	// Foreign C/C++ objects compiled by the interop pre-pass (`extern "c" source` files and their shims): a [link]
	// variable or extern function only resolves against them.
	for (const auto& foreign : m_In.interop.objects)
		if (!foreign.objPath.empty()) objects.Push(Core::String(foreign.objPath.c_str()));
	Core::Vector<Core::String> all = objects;
	for (const Core::String& object : runtime) all.Push(object);
	Core::Vector<Core::String> libraries;
	for (const auto& lib : m_In.interop.libraries) libraries.Push(Core::String(lib.c_str()));
	for (const auto& argument : m_In.options.linkArguments) libraries.Push(argument);
	const DebugInfoPaths debugInfo = DebugInfoPathsFor(out);
	const bool pdb = m_In.options.debugInfo && target.SeparateDebugFile();
	// Linked under another name, so a link that fails keeps the old program's .pdb; the program names the final one.
	if (pdb)
		for (const std::string& argument : target.DebugFileLinkArguments(debugInfo.partial, debugInfo.final)) libraries.Push(Core::String(argument.c_str()));
	const fs::path scriptHost = m_In.options.scriptHost ? Scripting::Toolchain::ScriptHost() : fs::path();
	if (m_In.options.scriptHost)
	{
		if (scriptHost.empty())
		{
			m_In.result.AddBackendError(Core::String(("--script-host needs the script host library, " + ScriptHostLibrary(target) +
				", which this install does not have").c_str()));
			return false;
		}
		for (const std::string& argument : target.ScriptHostLinkArguments(scriptHost)) libraries.Push(Core::String(argument.c_str()));
	}
	const LinkStamp stamp = StampFor(base, all, libraries, debugInfo);
	if (stamp.Current()) CORE_INFO("{0} is up to date: its link would read what it read", out);
	else
	{
		stamp.Forget();
		if (!target.Link({ objects, runtime, libraries, out, m_In.options.subsystem, m_In.options.verbose }))
		{
			Scripting::RemoveFile(debugInfo.partial.string());
			m_In.result.AddBackendError("failed to link " + Core::String(out.c_str()));
			return false;
		}
		CORE_INFO("Linked {0}", out);
		if (pdb && !PlaceDebugInfo(debugInfo)) return false;
		stamp.Record();
	}
	if (m_In.options.scriptHost && !ShipScriptHost(scriptHost)) return false;
	Timed("runtime+link", start);
	return true;
}

namespace
{
	// The library files a link's arguments name, by path or found in a -L folder; the system's, found through LIB, are
	// not among them.
	std::vector<std::string> LibraryFiles(const Core::Vector<Core::String>& libraries)
	{
		std::vector<std::string> folders;
		std::vector<std::string> names;
		for (const Core::String& library : libraries)
			for (const std::string& argument : Scripting::LinkerArguments(library))
			{
				if (argument.rfind("-libpath:", 0) == 0) folders.push_back(argument.substr(9));
				else if (!argument.empty() && argument[0] != '-' && argument[0] != '/') names.push_back(argument);
			}
		std::vector<std::string> files;
		for (const std::string& name : names)
		{
			if (Scripting::IsFile(name)) { files.push_back(name); continue; }
			for (const std::string& folder : folders)
				if (const std::string found = (fs::path(folder) / name).string(); Scripting::IsFile(found)) { files.push_back(found); break; }
		}
		return files;
	}
}

// A link reads its objects and libraries, the compiler (whose install ships the runtime libraries and the script host)
// and lld-link, and writes the program and its .pdb.
LinkStamp ProgramOutput::StampFor(const std::string& base, const Core::Vector<Core::String>& objects, const Core::Vector<Core::String>& libraries,
	const DebugInfoPaths& debugInfo) const
{
	std::vector<std::string> inputs = LibraryFiles(libraries);
	for (const Core::String& object : objects) inputs.push_back(object);
	inputs.push_back(Scripting::Toolchain::Executable().string());
	inputs.push_back(Scripting::Toolchain::LldLink().string());
	inputs.push_back(Scripting::Toolchain::Clang().string()); // a Linux program's link driver
	std::vector<std::string> arguments = { m_In.options.outputFile, std::to_string(static_cast<int>(m_In.options.subsystem)) };
	for (const Core::String& library : libraries) arguments.push_back(library);
	if (const char* lib = std::getenv("LIB")) arguments.push_back(lib);
	arguments.push_back(Scripting::Toolchain::WindowsSysroot().string());   // the MSVC and SDK libraries lld-link takes
	arguments.push_back(Scripting::TargetName(m_In.options.target.System()));
	arguments.push_back(Scripting::Toolchain::LinuxSysroot().string()); // the glibc a Linux link takes
	std::vector<std::string> outputs = { m_In.options.outputFile };
	if (m_In.options.debugInfo && m_In.options.target.Abi().SeparateDebugFile()) outputs.push_back(debugInfo.final.string());
	return LinkStamp(base + ".link", inputs, arguments, std::move(outputs));
}

ProgramOutput::DebugInfoPaths ProgramOutput::DebugInfoPathsFor(const Core::String& program)
{
	const fs::path final = fs::absolute(fs::path(program.c_str())).replace_extension(".pdb");
	return { final, fs::path(final.string() + ".partial") };
}

// The new .pdb over the old one, now that the program it describes is in place.
bool ProgramOutput::PlaceDebugInfo(const DebugInfoPaths& paths)
{
	if (!Scripting::RenameFile(paths.partial.string(), paths.final.string())) return true;
	Scripting::RemoveFile(paths.partial.string());
	m_In.result.AddBackendError(Core::String(("cannot replace " + paths.final.string() + " (is a debugger holding it?)").c_str()));
	return false;
}

// --script-host (docs/runtime/04 section 3): the thread_local accessors, the symbol table, and the interface that goes
// beside the program once it is built. The interface's build id covers the inline bodies: a program emitted in
// partitions takes them from the inline body cache with the partitions' bitcode (CollectInlineBodies), a program of one
// partition here. A program that uses the scripting API without it has no table to link against: an error that says
// what to do.
bool ProgramOutput::PrepareScriptHost(const std::vector<llvm::Module*>& partitions)
{
	llvm::Module& module = *partitions.front();
	if (!m_In.options.scriptHost)
	{
		for (const llvm::Module* partition : partitions)
			if (partition->getNamedGlobal("__c4_script_table"))
			{
				m_In.result.AddBackendError("this program uses the scripting API (Scripting::Host), which needs a script host: build it with --script-host");
				return false;
			}
		return true;
	}
	const auto start = std::chrono::steady_clock::now();
	for (llvm::Module* partition : partitions) Scripting::EmitThreadLocalAccessors(*partition);
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
	const std::vector<std::string> runtime = Scripting::LibraryRuntimeSymbols(model, m_In.compiler.GetSourceManager());
	listed->insert(listed->begin(), runtime.begin(), runtime.end());
	listed->insert(listed->begin(), prelude.begin(), prelude.end());
	m_HostBuild.emplace();
	m_HostBuild->reflectedTypes = Scripting::ReflectedTypeNames(model);
	m_HostBuild->debugInfo = m_In.options.debugInfo;
	if (partitions.size() > 1)
	{
		m_HostEntries = Scripting::HostEntries(partitions, *listed); // the table is an object of its own (HostTable)
		m_HostBuild->symbols = Scripting::HostNamesOf(m_HostEntries);
		m_AlwaysListed = std::move(*listed);
	}
	else
	{
		m_HostBuild->symbols = Scripting::HostSymbols(module, *listed);
		if (!Scripting::EmitHostTable(module, m_HostBuild->symbols, &error))
		{
			m_In.result.AddBackendError(Core::String(error.c_str()));
			return false;
		}
	}
	if (partitions.size() > 1)
	{
		Timed("script host", start);
		return true; // the interface waits for the inline bodies (CollectInlineBodies, FinishScriptHost)
	}
	m_HostBuild->inlineBodies = Scripting::InlineBodies(module, Scripting::HostNames(m_HostBuild->symbols));
	Timed("script host", start);
	return FinishInterface(module);
}

// A script host's inline bodies, each partition's from the inline body cache (D-81): an edit extracts only those of the
// partitions it changed. A partition's bodies name only what it lists itself and what every table lists.
bool ProgramOutput::CollectInlineBodies(const std::vector<llvm::Module*>& partitions, const std::vector<std::string>& bitcode,
	const std::string& base, const Scripting::ObjectSettings& settings)
{
	const auto start = std::chrono::steady_clock::now();
	const Scripting::InlineBodyCache cache(base + ".inline", CacheSalt(settings) + " " + Scripting::HostNamesSalt(m_AlwaysListed));
	const Scripting::HostNames names(m_HostBuild->symbols);
	std::string error;
	const auto chunks = cache.Chunks(partitions, bitcode, names, kInlineBodyChunksPerPartition, settings.threads, error);
	if (!chunks)
	{
		m_In.result.AddBackendError(Core::String(error.c_str()));
		return false;
	}
	for (const std::vector<std::string>& partition : *chunks)
		m_HostBuild->inlineBodies.insert(m_HostBuild->inlineBodies.end(), partition.begin(), partition.end());
	Timed("inline bodies", start);
	return true;
}

// The script table of a program emitted in partitions, an object of its own kept by what it lists: a partition changes
// only with its own code, and an edit that adds a function makes this small object again, not partition 0.
Scripting::KeyedModule ProgramOutput::HostTable(const llvm::Module& program) const
{
	const std::string triple = program.getTargetTriple().str(), layout = program.getDataLayoutStr();
	return { "host-table-" + Scripting::HostTableKey(m_HostEntries, triple + " " + layout),
		[this, triple, layout](llvm::LLVMContext& context) { return Scripting::HostTableModule(context, m_HostEntries, triple, layout); } };
}

// The interface, now that the inline bodies are in, and its build id in `module`.
bool ProgramOutput::FinishInterface(llvm::Module& module)
{
	std::string error;
	m_Interface = Scripting::BuildInterface(m_In.compiler, m_In.exportedMacros, std::move(*m_HostBuild), &error, &m_InterfaceBytes);
	m_HostBuild.reset();
	if (!m_Interface)
	{
		m_In.result.AddBackendError(Core::String(error.c_str()));
		return false;
	}
	Scripting::EmitBuildId(module, m_Interface->buildId);
	return true;
}

namespace
{
	std::string HexOf(const std::array<uint8, 16>& id)
	{
		static const char digits[] = "0123456789abcdef";
		std::string hex;
		for (const uint8 byte : id) hex += { digits[byte >> 4], digits[byte & 15] };
		return hex;
	}
}

// The interface of a build whose split extracted the inline bodies, and its build id as an object of its own, linked
// with the program's: kept by the id, so a build whose interface is unchanged links the same object (LinkStamp).
bool ProgramOutput::FinishScriptHost(const llvm::Module& program, const Scripting::ObjectSettings& settings, const std::string& base,
	std::vector<std::string>& objects)
{
	const auto start = std::chrono::steady_clock::now();
	llvm::Module buildId("build-id", program.getContext());
	buildId.setTargetTriple(program.getTargetTriple());
	buildId.setDataLayout(program.getDataLayout());
	if (!FinishInterface(buildId)) return false;
	const Scripting::ObjectCache kept(base + ".build-id", CacheSalt(settings));
	const std::string path = kept.ObjectKeyed(HexOf(m_Interface->buildId)).string();
	if (!Scripting::IsFile(path) && !WriteBuildId(buildId, settings, kept, path)) return false;
	objects.push_back(path);
	Timed("interface", start);
	return true;
}

bool ProgramOutput::WriteBuildId(llvm::Module& buildId, const Scripting::ObjectSettings& settings, const Scripting::ObjectCache& kept,
	const std::string& path)
{
	if (const std::error_code ec = Scripting::CreateFolders(kept.Folder().string()))
	{
		m_In.result.AddBackendError(Core::String(("cannot create " + kept.Folder().string() + ": " + ec.message()).c_str()));
		return false;
	}
	std::vector<Scripting::ObjectDiagnostic> diagnostics;
	const bool written = Scripting::WriteObject(buildId, settings, path, diagnostics);
	Report(diagnostics);
	if (!written) Scripting::RemoveFile(path); // never a partial object kept for the next build
	kept.KeepOnly({ std::filesystem::path(path) });
	return written;
}

bool ProgramOutput::WriteScriptInterface()
{
	const std::string interfacePath = fs::path(m_In.options.outputFile.c_str()).replace_extension(".c4i").string();
	std::string error;
	if (!Scripting::WriteSealedInterface(m_InterfaceBytes, interfacePath, &error))
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
	const std::string library = ScriptHostLibrary(m_In.options.target.Abi());
	const fs::path target = fs::path(m_In.options.outputFile.c_str()).parent_path() / library;
	const std::error_code ec = Scripting::UpdateFile((scriptHost / library).string(), target.string());
	if (!ec) return true;
	m_In.result.AddBackendError(Core::String(("cannot copy " + library + " beside the program: " + ec.message()).c_str()));
	return false;
}

void ProgramOutput::Timed(const char* phase, std::chrono::steady_clock::time_point since)
{
	m_In.compiler.AddPhaseTime(phase, std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count());
}
