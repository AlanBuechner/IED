#pragma once
#include "Core/Core.h"
#include "Compiler/AskedNames.h"
#include "Compiler/CpuTarget.h"
#include "Script/HostAssumptions.h"
#include "Script/HostInterface.h"
#include "Script/ScriptLayer.h"
#include "Script/ScriptSources.h"
#include <map>
#include <memory>
#include <optional>
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

	struct ScriptCompileOptions
	{
		std::string name;                           // the script's name, also its reflection tag
		std::vector<ScriptFile> files;
		std::vector<std::string> allow;             // the host headers a script file may include (section 4.5); empty: all
		bool onlyAllowed = false;                   // only what `allow` names (and the library), even when it names nothing
		std::map<std::string, std::string> defines;
		bool optimize = true;                       // -O2; false: -O0
		bool debugInfo = false;                     // CodeView for a debug script's PDB (section 6.4)
		std::optional<bool> debug;                  // this script debugged or not, whatever the host's setting says
		std::string project;                        // the project its files are in (section 5.2); empty: "script"
		std::vector<std::string> roots;             // the folders its includes resolve from; none: each file's own folder
		bool extensible = false;                    // other scripts may compile against and link to it (section 5.2)
		std::shared_ptr<const ScriptChain> base;    // the script it extends; null: its host alone
		bool baseMissing = false;                   // it was asked to extend a script that is not extensible
	};

	/// A compiled script, ready to link: its module (null when it did not compile) and its context, which outlives it.
	struct CompiledScript
	{
		std::unique_ptr<llvm::LLVMContext> context;
		std::unique_ptr<llvm::Module> module;
		std::vector<std::string> threadLocals; // the script's thread_local globals; LLVM's emulated TLS controls them through `__emutls_v.<name>`
		std::string diagnostics;               // every error and warning, as the compiler prints them
		std::optional<ScriptLayer> layer;      // an extensible script's (section 5.2)
		HostAssumptions assumptions;           // what its code assumed of the host (section 4.6); its wholeInterface the cache's to fill
		size_t interfaceFilesRead = 0;         // how many of the interface's and the layers' files it parsed (section 4, step 3)
		bool readWholeInterface = false;       // it met a collided symbol, and read every file
	};

	/// What a script compile asks of its host, kept as asked (section 4.6): the names of its table and the types it
	/// registered. Outlives the compiler instance that asks.
	struct HostQuestions
	{
		explicit HostQuestions(const HostIndex& index) : symbols(index.symbols), types(index.types) {}
		AskedNames symbols;
		AskedNames types;
	};

	/// How much of the host's interface (and its layers') a script's front end parses.
	enum class InterfaceParse
	{
		Reached, // what the script's project reaches (ReachedUnits): a compile
		Whole,   // every file: the language server, which answers for the interface's files too
	};

	/// Compiles a script against its host (docs/runtime/04 section 4): registers the interface and the script,
	/// preprocesses the script, builds the model, checks what a script may not do (an `extern` import, a header off
	/// the allow-list, defining a function its host defines), lowers it in script mode, imports the host's inline
	/// bodies it calls, and optimizes it for `machine` (the JIT's). Never throws; the diagnostics say what failed.
	CompiledScript CompileScript(const HostInterface& host, const HostIndex& index, const ScriptCompileOptions& options, llvm::TargetMachine& machine);

	/// The front end of a script compile alone (section 4, steps 1 to 3) into `compiler`, which then holds the script's
	/// model and the compiler's own diagnostics: registered, preprocessed, parsed (as much of the interface as `parse`
	/// says) and analyzed against `host`, asking `questions`, with what a script may not do (an `extern` import, a header
	/// off the allow-list, a body for a host function) in `checks`. The language server analyzes scripts with it. Nothing,
	/// with why in `error`, when the files cannot be registered.
	std::optional<ScriptUnits> AnalyzeScript(CompilerInstance& compiler, const HostInterface& host, const HostIndex& index,
		const HostQuestions& questions, const ScriptCompileOptions& options, InterfaceParse parse, ErrorTracker& checks, std::string* error);

	/// Whether `include` (an interface file) is one the allow-list lets a script include: a library file always, any
	/// file when the list is empty and not `onlyAllowed`, else a file whose project is named on the list or whose path
	/// starts with an entry.
	bool IncludeAllowed(const HostInterface& host, const InterfaceFile& include, const std::vector<std::string>& allow, bool onlyAllowed);
} // namespace Scripting
