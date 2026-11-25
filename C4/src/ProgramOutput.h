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

/// What a build server keeps of a partition's output (docs/compiler/07): for a partition a later build does not emit
/// again, its object, its inline bodies and its candidates for the script table, as this build wrote them.
struct PartitionOutput
{
	std::string object;
	std::filesystem::path inlineFile;
	std::vector<std::string> inlineBodies;
	std::vector<C4Compiler::HostCandidate> hostCandidates;
};

/// A build server's outputs from build to build: each partition's, and which partitions this build emitted (the others'
/// modules are empty: their output is the kept one).
struct ServedOutputs
{
	std::vector<PartitionOutput> partitions;
	std::vector<bool> emitted; // empty: every one

	bool Reused(size_t partition) const { return !emitted.empty() && !emitted[partition]; }
	/// Whether every partition has its object still (one not emitted again is linked from it).
	bool ObjectsExist(size_t count) const;
};

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
		const C4Compiler::CompileOptions& options;
		C4Compiler::CompilationResult& result;
		C4Compiler::CompilerInstance& compiler;
		const C4Compiler::InteropPass::Result& interop;
		const std::unordered_map<std::string, C4Compiler::MacroTable>& exportedMacros;
		ServedOutputs* served = nullptr; // a build server's: partitions not emitted again are the kept ones
	};

	explicit ProgramOutput(const Inputs& inputs) : m_In(inputs) {}

	/// The program's output from its partitions (D-81; the first holds the program's own code and data, the only one
	/// unless the build links) and its reflection image (D-82): a module of its own, built only when no object of its
	/// key is kept (no key: the image is in the first partition).
	bool Write(const std::vector<llvm::Module*>& partitions, const C4Compiler::KeyedModule& image = {});

	/// Whether the build links a program (not `-S`, `-bc` or `-c`, which write the one module): then it is emitted in
	/// partitions, and each is verified as the output reads it (so lowering leaves it unverified: ProgramBackend::VerifyLater).
	static bool Links(const C4Compiler::CompileOptions& options);

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
	bool WriteWholeProgram(const std::vector<llvm::Module*>& partitions, const C4Compiler::KeyedModule& image, const std::string& base);
	std::unique_ptr<llvm::Module> Merged(const std::vector<llvm::Module*>& partitions, const C4Compiler::KeyedModule& image,
		const std::string& base, const C4Compiler::ObjectSettings& settings);
	bool Verified(const llvm::Module& module);
	bool WritePartitions(const std::vector<llvm::Module*>& partitions, const C4Compiler::KeyedModule& image, const std::string& base);
	bool WriteIR(const llvm::Module& module, const std::string& path);
	bool WriteBitcode(const llvm::Module& module, const std::string& path);
	bool Optimize(llvm::Module& module, const C4Compiler::ObjectSettings& settings);
	unsigned PartitionsFor(const llvm::Module& module, const C4Compiler::ObjectSettings& settings) const;
	static std::string CacheSalt(const C4Compiler::ObjectSettings& settings);
	bool WriteCompiledObject(llvm::Module& module, const C4Compiler::ObjectSettings& settings, const std::string& objPath);
	C4Compiler::CachedObjects Assemble(llvm::Module& module, const C4Compiler::ObjectSettings& settings, const C4Compiler::ObjectCache& cache);
	void ReportPartitions(const C4Compiler::CachedObjects& objects, unsigned partitions, unsigned threads) const;
	void ReportObjects(uint64_t instructions, unsigned objects, size_t reused, unsigned threads, const C4Compiler::ObjectCache& cache) const;
	void Report(const std::vector<C4Compiler::ObjectDiagnostic>& diagnostics);
	bool Link(const std::vector<std::string>& programObjects, const std::string& base);
	/// Link, and when it fails with objects found in the cache, again with those assembled again (BUG-508): a partition's
	/// bitcode from `bitcode` (null when every kept object carries its own).
	bool LinkKept(const C4Compiler::CachedObjects& objects, const std::function<std::string(size_t)>& bitcode,
		const C4Compiler::ObjectSettings& settings, const std::string& base);
	void Warn(const std::string& message);
	LinkStamp StampFor(const std::string& base, const Core::Vector<Core::String>& objects, const Core::Vector<Core::String>& libraries,
		const DebugInfoPaths& debugInfo) const;
	static DebugInfoPaths DebugInfoPathsFor(const Core::String& program);
	bool PlaceDebugInfo(const DebugInfoPaths& paths);
	bool PrepareScriptHost(const std::vector<llvm::Module*>& partitions);
	/// A build server's partition not emitted again: its output is the kept one (ServedOutputs).
	bool Reused(size_t partition) const;
	std::vector<C4Compiler::HostEntry> HostEntriesOf(const std::vector<llvm::Module*>& partitions, const std::vector<std::string>& listed);
	bool CollectInlineBodies(const std::vector<llvm::Module*>& partitions, const std::vector<std::string>& bitcode, const std::string& base,
		const C4Compiler::ObjectSettings& settings);
	C4Compiler::KeyedModule HostTable(const llvm::Module& program) const;
	bool FinishInterface(llvm::Module& module);
	bool FinishScriptHost(const llvm::Module& program, const C4Compiler::ObjectSettings& settings, const std::string& base,
		std::vector<std::string>& objects);
	bool WriteBuildId(llvm::Module& buildId, const C4Compiler::ObjectSettings& settings, const C4Compiler::ObjectCache& kept,
		const std::string& path);
	bool WriteScriptInterface();
	bool ShipScriptHost(const std::filesystem::path& scriptHost);
	void Timed(const char* phase, std::chrono::steady_clock::time_point since);

	/// Of a program emitted in partitions, at most this many chunks of each partition's inline bodies.
	static constexpr unsigned kInlineBodyChunksPerPartition = 4;

	Inputs m_In;
	std::optional<C4Compiler::HostBuild> m_HostBuild;       // a script host's, until its inline bodies are in
	std::vector<C4Compiler::HostEntry> m_HostEntries;       // a program in partitions: what its table lists
	std::vector<std::string> m_AlwaysListed;               // and the names every table lists beside the runtime's
	std::optional<C4Compiler::HostInterface> m_Interface;   // what goes beside the program
	std::string m_InterfaceBytes;                          // its bytes, made once with its build id
};
