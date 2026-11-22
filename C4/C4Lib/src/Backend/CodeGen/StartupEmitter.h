#pragma once
#include "Backend/CodeGen/PartitionSet.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Lowering/ScriptLowering.h"
#include <deque>
#include <string>
#include <vector>

namespace C4Compiler
{
	/// The program's code around main, in partition 0: `__c4_global_init`, which runs the rounds of starts before main
	/// (a global whose start is constant data starts holding it instead), `__c4_thread_start`, which a thread runs as it
	/// attaches; a script's `__c4_script_init` and its thread_locals' starts on first use; the `@main` that calls the
	/// program's entry; the GC statistics printer.
	class StartupEmitter
	{
	public:
		explicit StartupEmitter(PartitionSet& partitions) : m_Partitions(partitions) {}

		/// A script's starts are its own (docs/runtime/04 section 4.2): its rounds run from `__c4_script_init`, which its
		/// loader calls, and its thread_locals start on their first use.
		void ForScript(const ScriptLowering* script) { m_Script = script; }

		/// The rounds and first-use starts of `units`, in order, each named for its kind and turn, and what runs them.
		/// False, with `error`, when a thread_local a script's code starts on first use has no start.
		bool EmitStarts(std::vector<LoweredUnit>& units, ProgramModel& model, std::string* error);
		/// The linker looks for a literal `@main`; the program's entry has an overload suffix (`main_0`), so a thin
		/// `@main` forwards to it. False, with `error`, when the program has no entry named `entryName`.
		bool EmitEntryPoint(const ProgramModel& model, const Core::String& entryName, std::string* error);
		/// `-gc-stats`: before main, the program installs the GC's default statistics printer.
		void InstallGcStatsPrinter();

	private:
		/// The functions of one kind of round, and what they are named.
		struct Rounds
		{
			std::string prefix;
			std::vector<llvm::Function*> functions;
		};

		void EmitRound(ProgramModel& model, LoweredUnit& round, Rounds& rounds);
		void EmitFirstUseStart(ProgramModel& model, LoweredUnit& start);
		/// Each global's data goes into its storage, in the partition that defines it; one whose storage's type cannot
		/// hold it starts in `round` after all.
		void StartAsData(std::vector<ConstantStart>& constants, HIRFunction& round);
		/// `void name()` running `body`, a start the compiler wrote.
		llvm::Function* EmitStartFunction(ProgramModel& model, HIRFunction& body, const std::string& name);
		bool EveryFirstUseStartDefined(std::string* error);
		void RegisterGlobalInit(const std::vector<llvm::Function*>& rounds);
		void RegisterThreadStart(const std::vector<llvm::Function*>& rounds);
		/// A `void name()` calling each of `calls`, then `last` (when there is one).
		llvm::Function* CallingInOrder(const char* name, const std::vector<llvm::Function*>& calls, llvm::Function* last);
		void EmitMainWrapper(const FunctionRecord& entry);

		PartitionSet& m_Partitions;
		const ScriptLowering* m_Script = nullptr;
		std::vector<llvm::Function*> m_FirstUseStarts; // a script's thread_locals' starts on first use
		std::deque<FunctionRecord> m_Records; // the starts' records: compiler-written, no model record
	};
} // namespace C4Compiler
