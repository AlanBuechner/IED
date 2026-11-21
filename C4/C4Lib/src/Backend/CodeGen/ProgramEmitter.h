#pragma once
#include "Backend/CodeGen/PartitionSet.h"
#include "Backend/CodeGen/ReflectionEmitter.h"
#include "Backend/CodeGen/StartupEmitter.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Lowering/ScriptLowering.h"
#include "Compiler/ErrorTracker.h"
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace Scripting
{
	class ProgramModel;

	/// How a lowered program is emitted, beyond what it holds.
	struct EmitSettings
	{
		ModuleSettings modules;
		bool verifyLater = false;          // each partition is verified as it is assembled (ObjectSettings::verify)
		bool onlyAlwaysInline = false;     // -O0: a partition copies only `inline` functions from the others
		Core::String entryName;            // the function `@main` calls; empty for none (a library, a script)
		const TestHarnessOptions* tests = nullptr; // a -test build: a program with tests gets the harness as its main
		Core::String reflectionTag;        // `-reflect-tag`: what the program's reflection registers under
		const ScriptLowering* script = nullptr; // a script (docs/runtime/04 section 4.2): the host's definitions win
	};

	/// Emits a lowered program (ProgramLowering) as LLVM modules (docs/compiler/04, 05): every tree into its partition,
	/// the partitions at once; the startup code, the vtables `--script-host` shares, the storage of every global, the
	/// reflection image, each partition's copies of the small functions it calls from the others, the GC roots and the
	/// target's attributes. It decides nothing about what is lowered, and checks that what its code names was.
	class ProgramEmitter
	{
	public:
		ProgramEmitter(llvm::LLVMContext& context, TargetSystem system)
			: m_Partitions(context, system), m_Startup(m_Partitions), m_Reflection(m_Partitions) {}

		/// The program is emitted into `count` partitions (D-81). Before Emit.
		void SplitInto(unsigned count) { m_Partitions.SplitInto(count); }
		/// False, with `error`, when the program has no entry point, a module does not verify, or codegen named what
		/// lowering did not reach (an internal error); with the reflection image's diagnostics in Errors().
		bool Emit(ProgramModel& model, LoweredProgram& program, const EmitSettings& settings, WorkerPool& pool, std::string* error);

		/// The diagnostics of the reflection image (an annotation argument that does not fold).
		const ErrorTracker& Errors() const { return m_Errors; }
		/// How long each step took (-ftime).
		const std::vector<std::pair<const char*, double>>& StepSeconds() const { return m_StepSeconds; }
		PartitionSet& Partitions() { return m_Partitions; }
		ReflectionEmitter& Reflection() { return m_Reflection; }
		const ReflectionEmitter& Reflection() const { return m_Reflection; }
		StartupEmitter& Startup() { return m_Startup; }

	private:
		void EmitFileAssembly(const ProgramModel& model);
		/// A runtime or interop name is declared once, by its record; template-made and compiler-written functions are
		/// declared when code first names them.
		void DeclareWrittenFunctions(const ProgramModel& model);
		std::vector<Placed> Placements(const LoweredProgram& program) const;
		/// `--script-host`: the classes' vtables, whether or not the program makes one of them (a script's objects of
		/// the program's classes use the program's).
		void EmitSharedVtables(const LoweredProgram& program);
		bool EmitEntry(ProgramModel& model, const EmitSettings& settings, std::string* error);
		/// The storage of every global and static member, whether or not code names it: reflection registers each.
		void MaterializeGlobals(const ProgramModel& model);
		bool EverythingNamedLowered(const LoweredProgram& program, std::string* error);
		void FinishModules(const ProgramModel& model, const EmitSettings& settings, WorkerPool& pool);
		/// The script-mode passes over the finished module (Script/ScriptModulePasses).
		void ApplyScriptLowering(const ScriptLowering& script);
		void Timed(const char* step, std::chrono::steady_clock::time_point& since);

		PartitionSet m_Partitions;
		StartupEmitter m_Startup;
		ReflectionEmitter m_Reflection;
		ErrorTracker m_Errors;
		std::vector<std::pair<const char*, double>> m_StepSeconds;
	};
} // namespace Scripting
