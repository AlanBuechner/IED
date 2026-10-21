#pragma once
#include "Core/Core.h"
#include "Compiler/CpuTarget.h"
#include "Script/HostInterface.h"
#include "Script/ScriptSources.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace llvm
{
	class LLVMContext;
	class Module;
	class TargetMachine;
}

namespace Scripting
{
	class CompilerInstance;
	class ErrorTracker;

	/// A host interface's lookups, made once for every compile against it.
	struct HostIndex
	{
		explicit HostIndex(const HostInterface& host);
		std::unordered_set<std::string> symbols; // the table's entries
		std::unordered_set<std::string> types;   // the types the host's reflection registered
		CpuTarget target;                        // the host's processor: every script compiles for it (D-62)
	};

	struct ScriptCompileOptions
	{
		std::string name;                           // the script's name, also its reflection tag
		std::vector<ScriptFile> files;
		std::vector<std::string> allow;             // the host headers a script file may include (section 4.5); empty: all
		std::map<std::string, std::string> defines;
		bool optimize = true;                       // -O2; false: -O0
		bool debugInfo = false;                     // CodeView for a debug script's PDB (section 6.4)
	};

	/// A compiled script, ready to link: its module (null when it did not compile) and its context, which outlives it.
	struct CompiledScript
	{
		std::unique_ptr<llvm::LLVMContext> context;
		std::unique_ptr<llvm::Module> module;
		std::vector<std::string> threadLocals; // the script's thread_local globals; LLVM's emulated TLS controls them through `__emutls_v.<name>`
		std::string diagnostics;               // every error and warning, as the compiler prints them
	};

	/// Compiles a script against its host (docs/runtime/04 section 4): registers the interface and the script,
	/// preprocesses the script, builds the model, checks what a script may not do (an `extern` import, a header off
	/// the allow-list, defining a function its host defines), lowers it in script mode, imports the host's inline
	/// bodies it calls, and optimizes it for `machine` (the JIT's). Never throws; the diagnostics say what failed.
	CompiledScript CompileScript(const HostInterface& host, const HostIndex& index, const ScriptCompileOptions& options, llvm::TargetMachine& machine);

	/// The front end of a script compile alone (section 4, steps 1 to 3) into `compiler`, which then holds the script's
	/// model and the compiler's own diagnostics: registered, preprocessed, parsed and analyzed against `host`, with what a
	/// script may not do (an `extern` import, a header off the allow-list, a body for a host function) in `checks`. The
	/// language server analyzes scripts with it. Nothing, with why in `error`, when the files cannot be registered.
	std::optional<ScriptUnits> AnalyzeScript(CompilerInstance& compiler, const HostInterface& host, const HostIndex& index,
		const ScriptCompileOptions& options, ErrorTracker& checks, std::string* error);

	/// Whether `include` (an interface file) is one the allow-list lets a script include: a library file always, any
	/// file when the list is empty, else a file whose project is named on the list or whose path starts with an entry.
	bool IncludeAllowed(const HostInterface& host, const InterfaceFile& include, const std::vector<std::string>& allow);
} // namespace Scripting
