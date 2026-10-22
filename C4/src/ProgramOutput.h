#pragma once
#include "Compiler/BuildUtils.h"
#include "Compiler/CompilationResult.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ObjectCache.h"
#include "Compiler/ObjectEmission.h"
#include "Interop/InteropPass.h"
#include "Macro/MacroTable.h"
#include "Script/HostInterface.h"
#include "Script/InterfaceBuilder.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm { class Module; }

/// The last stage of a build (docs/tools/01-compiler-cli.md): the finished module as IR (`-S`), bitcode (`-bc`) or
/// objects the compiler optimizes and assembles itself (in parallel for a large module), then linked with the C4
/// runtime into the program. A script host
/// also gets its symbol table, its `.c4i` interface and C4Script.dll beside it.
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

	/// The module a split program's reflection image is built in, waited for (D-82): null when it has none.
	using ImageModule = std::function<llvm::Module*()>;

	/// The program's output from its module, or, emitted in partitions (D-81), its partitions' (the first holding the
	/// program's own code and data) and `image`'s, which is taken last, when its bitcode is wanted.
	bool Write(const std::vector<llvm::Module*>& partitions, const ImageModule& image = nullptr);

	/// Whether the program's partitions are optimized each alone, after the split, and verified as they are assembled
	/// (so lowering leaves the module unverified: CodeGenDriver::VerifyLater).
	static bool OptimizesPartitions(const Scripting::CompileOptions& options);

private:
	// The .pdb a link writes, and the name it is written under until the program is in place.
	struct DebugInfoPaths
	{
		std::filesystem::path final;
		std::filesystem::path partial;
	};

	bool MakeOutputFolder();
	std::string IntermediateBase();
	bool WriteProgram(llvm::Module& module, const std::string& base);
	bool WriteSplitProgram(std::vector<llvm::Module*> partitions, const ImageModule& image, const std::string& base);
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
	bool Link(const std::vector<std::string>& programObjects);
	static DebugInfoPaths DebugInfoPathsFor(const Core::String& program);
	bool PlaceDebugInfo(const DebugInfoPaths& paths);
	bool PrepareScriptHost(const std::vector<llvm::Module*>& partitions);
	bool FinishInterface(llvm::Module& module);
	bool FinishScriptHost(const llvm::Module& program, const Scripting::ObjectSettings& settings, const std::string& base,
		std::vector<std::string>& objects);
	bool WriteScriptInterface();
	bool ShipScriptHost(const std::filesystem::path& scriptHost);
	void Timed(const char* phase, std::chrono::steady_clock::time_point since);

	/// At most this many chunks of inline bodies, extracted on the split's threads.
	static constexpr unsigned kInlineBodyChunks = 64;
	/// Of a program emitted in partitions, at most this many of each partition's.
	static constexpr unsigned kInlineBodyChunksPerPartition = 4;

	Inputs m_In;
	std::optional<Scripting::HostBuild> m_HostBuild;       // a script host's, until its inline bodies are in
	Scripting::Extracts m_InlineBodies;                    // the inline bodies the split extracts for it
	std::optional<Scripting::HostInterface> m_Interface;   // what goes beside the program
	std::string m_InterfaceBytes;                          // its bytes, made once with its build id
};
