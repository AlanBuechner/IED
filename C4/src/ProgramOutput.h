#pragma once
#include "LinkStamp.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/CompilationResult.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ObjectCache.h"
#include "Compiler/ObjectEmission.h"
#include "Interop/InteropPass.h"
#include "Macro/MacroTable.h"
#include "Script/HostInterface.h"
#include "Script/HostTable.h"
#include "Script/InterfaceBuilder.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm { class Module; }

/// The last stage of a build (docs/tools/01-compiler-cli.md): the program's module as IR (`-S`), bitcode (`-bc`) or one
/// object (`-c`); else its partitions (D-81) optimized each alone (an incremental build) or merged and optimized whole (a
/// whole-program build, R12), assembled inside the compiler on several threads and linked with the C4 runtime into the
/// program. A script host also gets its symbol table, its `.c4i` interface and C4Script.dll beside it.
class ProgramOutput
{
public:
	/// What the earlier stages produced that the output needs.
	struct Inputs
	{
		const Scripting::CompileOptions& options;
		Scripting::CompilationResult& result;
		Scripting::CompilerInstance& compiler;
		const Scripting::InteropPass::Result& interop;
		const std::unordered_map<std::string, Scripting::MacroTable>& exportedMacros;
	};

	explicit ProgramOutput(const Inputs& inputs) : m_In(inputs) {}

	/// The program's output from its partitions (D-81; the first holds the program's own code and data, the only one
	/// unless the build links) and its reflection image (D-82): a module of its own, built only when no object of its
	/// key is kept (no key: the image is in the first partition).
	bool Write(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image = {});

	/// Whether the build links a program (not `-S`, `-bc` or `-c`, which write the one module): then it is emitted in
	/// partitions, and each is verified as the output reads it (so lowering leaves it unverified: ProgramBackend::VerifyLater).
	static bool Links(const Scripting::CompileOptions& options);

private:
	// The .pdb a link writes, and the name it is written under until the program is in place.
	struct DebugInfoPaths
	{
		std::filesystem::path final;
		std::filesystem::path partial;
	};

	bool MakeOutputFolder();
	std::string IntermediateBase();
	bool WriteModule(llvm::Module& module, const std::string& base);
	bool WriteWholeProgram(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image, const std::string& base);
	std::unique_ptr<llvm::Module> Merged(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image,
		const std::string& base, const Scripting::ObjectSettings& settings);
	bool Verified(const llvm::Module& module);
	bool WritePartitions(const std::vector<llvm::Module*>& partitions, const Scripting::KeyedModule& image, const std::string& base);
	bool WriteIR(const llvm::Module& module, const std::string& path);
	bool WriteBitcode(const llvm::Module& module, const std::string& path);
	bool Optimize(llvm::Module& module, const Scripting::ObjectSettings& settings);
	unsigned PartitionsFor(const llvm::Module& module, const Scripting::ObjectSettings& settings) const;
	static std::string CacheSalt(const Scripting::ObjectSettings& settings);
	bool WriteCompiledObject(llvm::Module& module, const Scripting::ObjectSettings& settings, const std::string& objPath);
	std::vector<std::string> Assemble(llvm::Module& module, const Scripting::ObjectSettings& settings, const Scripting::ObjectCache& cache);
	void ReportPartitions(const Scripting::CachedObjects& objects, unsigned partitions, unsigned threads) const;
	void ReportObjects(uint64_t instructions, unsigned objects, size_t reused, unsigned threads, const Scripting::ObjectCache& cache) const;
	void Report(const std::vector<Scripting::ObjectDiagnostic>& diagnostics);
	bool Link(const std::vector<std::string>& programObjects, const std::string& base);
	LinkStamp StampFor(const std::string& base, const Core::Vector<Core::String>& objects, const Core::Vector<Core::String>& libraries,
		const DebugInfoPaths& debugInfo) const;
	static DebugInfoPaths DebugInfoPathsFor(const Core::String& program);
	bool PlaceDebugInfo(const DebugInfoPaths& paths);
	bool PrepareScriptHost(const std::vector<llvm::Module*>& partitions);
	bool CollectInlineBodies(const std::vector<llvm::Module*>& partitions, const std::vector<std::string>& bitcode, const std::string& base,
		const Scripting::ObjectSettings& settings);
	Scripting::KeyedModule HostTable(const llvm::Module& program) const;
	bool FinishInterface(llvm::Module& module);
	bool FinishScriptHost(const llvm::Module& program, const Scripting::ObjectSettings& settings, const std::string& base,
		std::vector<std::string>& objects);
	bool WriteBuildId(llvm::Module& buildId, const Scripting::ObjectSettings& settings, const Scripting::ObjectCache& kept,
		const std::string& path);
	bool WriteScriptInterface();
	bool ShipScriptHost(const std::filesystem::path& scriptHost);
	void Timed(const char* phase, std::chrono::steady_clock::time_point since);

	/// Of a program emitted in partitions, at most this many chunks of each partition's inline bodies.
	static constexpr unsigned kInlineBodyChunksPerPartition = 4;

	Inputs m_In;
	std::optional<Scripting::HostBuild> m_HostBuild;       // a script host's, until its inline bodies are in
	std::vector<Scripting::HostEntry> m_HostEntries;       // a program in partitions: what its table lists
	std::vector<std::string> m_AlwaysListed;               // and the names every table lists beside the runtime's
	std::optional<Scripting::HostInterface> m_Interface;   // what goes beside the program
	std::string m_InterfaceBytes;                          // its bytes, made once with its build id
};
