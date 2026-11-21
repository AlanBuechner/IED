#pragma once
#include "Backend/CodeGen/ProgramEmitter.h"
#include "Backend/Lowering/ProgramLowering.h"
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Scripting
{
	/// A program's backend (docs/compiler/01): ProgramLowering lowers what the program needs, then ProgramEmitter emits
	/// it as LLVM modules - what C4.exe and a script's compile run. The language server runs the same lowering alone.
	class ProgramBackend
	{
	public:
		/// A backend emitting the program for `system` (D-86): the build's, or the running host's for a program that runs
		/// here (a script, a test's).
		explicit ProgramBackend(llvm::LLVMContext& context, TargetSystem system = HostSystem()) : m_Emitter(context, system) {}

		/// Bodies are lowered, and partitions emitted, on up to `threads` threads, with the result of doing it on one.
		void LowerOn(unsigned threads) { m_Lowering.threads = threads; }
		/// `-g` (B28): the modules carry debug information (CodeView).
		void EnableDebugInfo() { m_Emit.modules.debugInfo = true; }
		/// `-fno-strict-aliasing` (D-64): loads and stores carry no alias tags, so any may touch any memory.
		void DisableStrictAliasing() { m_Emit.modules.strictAliasing = false; }
		/// `-reflect-tag`: the program's reflection registers under `tag` (a hot-reload source) instead of "".
		void SetReflectionTag(const Core::String& tag) { m_Emit.reflectionTag = tag; }
		/// `--script-host` (docs/runtime/04 section 3.1): every class with virtual methods gets its vtables whether or not
		/// the program makes one, so a script's objects of the program's classes use the program's, which outlive it.
		void ShareVtables() { m_Lowering.everyVtable = true; }
		/// The modules are left unverified: the build verifies each partition as it assembles it, on its threads.
		void VerifyLater() { m_Emit.verifyLater = true; }
		/// The program is emitted into `count` partitions (D-81), each copying the small functions it calls from the
		/// others (only `inline` ones with `onlyAlwaysInline`, -O0).
		void SplitInto(unsigned count, bool onlyAlwaysInline);
		/// A script (docs/runtime/04 section 4.2), with no entry point: the host's definitions and storage win, the host's
		/// globals are not started, the script's start from `__c4_script_init`, the reflection image registers through
		/// the loader, and exits and throws go through the script runtime.
		void EnableScriptMode(ScriptLowering script) { m_Lowering.script = script; }

		/// Lowers and emits the program. `entryName` names the function `@main` calls ("": none, a library or a script);
		/// with `tests` (a -test build) a program that has tests gets the test harness as its main. False when a body
		/// fails to lower (LoweringErrors()) or emission fails (`errorMessage` says why).
		bool Lower(ProgramModel& model, const Core::String& entryName, std::string* errorMessage = nullptr, const TestHarnessOptions* tests = nullptr);

		/// The located diagnostics of lowering and of the reflection image, warnings too.
		const Core::Vector<CompilationError>& LoweringErrors() const { return m_Errors.GetErrors(); }
		const LoweringStats& LoweringStatistics() const;
		/// How long each step took (-ftime): lowering's, then emission's.
		const std::vector<std::pair<const char*, double>>& StepSeconds() const { return m_StepSeconds; }

		/// Partition 0, the program's module, owned by the backend until TakeModule.
		llvm::Module* module() { return &m_Emitter.Partitions().Partition(0).module(); }
		std::unique_ptr<llvm::Module> TakeModule() { return m_Emitter.Partitions().Partition(0).TakeModule(); }
		unsigned PartitionCount() const { return m_Partitions; }
		llvm::Module& PartitionModule(unsigned i) { return m_Emitter.Partitions().Partition(i).module(); }
		/// A split program's reflection image is an object of its own (D-82), kept by this key; empty for a program that
		/// is not split or registers nothing.
		const std::string& ReflectionImageKey() { return m_Emitter.Reflection().ImageKey(); }
		std::unique_ptr<llvm::Module> BuildReflectionModule(llvm::LLVMContext& context) const;
		/// `-gc-stats`: before main, the program installs the GC's default statistics printer.
		void InstallGcStatsPrinter() { m_Emitter.Startup().InstallGcStatsPrinter(); }

	private:
		bool LowerProgram(ProgramModel& model, std::string* errorMessage);
		/// The lowering failures as one message; false when there are none.
		bool Failed(std::string* errorMessage) const;

		LoweringOptions m_Lowering;
		EmitSettings m_Emit;
		unsigned m_Partitions = 1;
		std::unique_ptr<ProgramLowering> m_Lowered; // made by Lower: it holds the model
		ProgramEmitter m_Emitter;
		ErrorTracker m_Errors;
		std::vector<std::pair<const char*, double>> m_StepSeconds;
	};
} // namespace Scripting
