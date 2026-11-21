#pragma once
#include "Backend/CodeGen/FunctionEmitter.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/PartitionEmitter.h"
#include "Backend/CodeGen/ProgramPlacement.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Compiler/WorkerPool.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Scripting
{
	class ProgramModel;
	struct ScriptLowering;

	/// A lowered tree and the partition it is emitted into.
	struct Placed
	{
		const HIRFunction* tree = nullptr;
		unsigned partition = 0;
	};

	/// What every module of the program is emitted with.
	struct ModuleSettings
	{
		bool debugInfo = false;      // `-g` (B28): CodeView debug information
		bool strictAliasing = true;  // `-fno-strict-aliasing` (D-64) turns the alias tags off
	};

	/// The modules a program is emitted into (D-81): partition 0, the program's module, with its own code and data, and
	/// for a split program the others, each a module in an LLVM context of its own, every function and global in the one
	/// ProgramPlacement gives it. The partitions emit at once, each its trees in the order given, so a partition's
	/// bitcode does not depend on the threads.
	class PartitionSet
	{
	public:
		PartitionSet(llvm::LLVMContext& context, TargetSystem system) : m_First(context, system), m_FirstEmitter(m_First) {}

		/// `count` partitions; one keeps the program in one module. Before Prepare.
		void SplitInto(unsigned count);
		unsigned Count() const { return 1 + static_cast<unsigned>(m_Others.size()); }
		bool Split() const { return !m_Others.empty(); }
		ModuleContext& Partition(unsigned i) { return i == 0 ? m_First : m_Others[i - 1]->mctx(); }
		FunctionEmitter& EmitterOf(unsigned i) { return i == 0 ? m_FirstEmitter : m_Others[i - 1]->emitter(); }
		std::vector<llvm::Module*> Modules();
		unsigned Of(const FunctionRecord& rec) const { return m_Placement ? m_Placement->Of(rec) : 0; }
		unsigned Of(const SymbolInfo& global) const { return m_Placement ? m_Placement->Of(global) : 0; }

		/// Each module ready for the program's code: where a split program's functions and globals go, the model it reads
		/// layouts from, the function pointers' starts and census counters `program` has, and its object types.
		void Prepare(const ProgramModel& model, const LoweredProgram& program, const ModuleSettings& settings, WorkerPool& pool);
		/// Each tree into its partition, the partitions at once, each its trees in the order given; then each partition
		/// defines its own copy of every census counter its code calls.
		void Emit(const ProgramModel& model, const std::vector<Placed>& trees, WorkerPool& pool);
		/// Each partition defines the census counters the code it emitted since calls (a counter is a local of each).
		void DefineCensusCounters(WorkerPool& pool);
		/// Each partition's `copies` of functions the others define (FunctionEmitter::EmitImport), the partitions at once;
		/// what a copy names was named where its function is defined, but for the census counters, which each defines.
		void EmitCopies(const ProgramModel& model, const std::vector<std::vector<const HIRFunction*>>& copies, WorkerPool& pool);
		/// What code emitted since the last call names that `program` did not lower - a function with a body of its own (a
		/// nested one comes with its parent), a census counter - each by its name: none, unless lowering and codegen
		/// disagree about what code names (an internal error). A script's (`script` set) names of its host's functions are the
		/// host's code, which it never lowers (a [compile_time] one has its body in the interface).
		std::vector<std::string> NamedButNotLowered(const LoweredProgram& program, const ScriptLowering* script);
		/// The static locals every other partition defines, declared in partition 0 for its GC roots.
		void DeclareOthersStaticStorage();
		/// The verifier's diagnostic of the first invalid module, naming the culprit; none when every module is valid.
		std::optional<std::string> FirstInvalid();

	private:
		/// Every object type laid out before partitions read layouts at once (a type is laid out on its first query).
		void SettleLayouts(const ProgramModel& model) const;
		void PrepareOne(unsigned i, const ProgramModel& model, const LoweredProgram& program, const ModuleSettings& settings);
		void DefineCensusCounters(unsigned i);

		ModuleContext m_First;
		FunctionEmitter m_FirstEmitter;
		std::vector<std::unique_ptr<PartitionEmitter>> m_Others; // partitions 1..n-1 of a split program
		std::optional<ProgramPlacement> m_Placement;            // where a split program's functions and globals go
	};
} // namespace Scripting
