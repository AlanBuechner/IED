#pragma once
#include "Backend/Lowering/GlobalStarts.h"
#include "Backend/Lowering/InvokeThunkPlan.h"
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Lowering/ParallelLowering.h"
#include "Backend/Lowering/ProgramReach.h"
#include "Backend/Lowering/ScriptLowering.h"
#include "Compiler/ErrorTracker.h"
#include <chrono>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace C4Compiler
{
	class ProgramModel;

	/// What decides what a program lowers, beyond its model.
	struct LoweringOptions
	{
		unsigned threads = 1;                 // bodies lowered at once (ParallelLowering); the program is the same whatever
		bool everyVtable = false;             // `--script-host`: every class with virtual methods has its vtables
		std::optional<ScriptLowering> script; // a script, compiled against its host (docs/runtime/04)
	};

	/// Lowers a program to HIR (docs/compiler/01, 03): what it needs, and in what order, for C4.exe, the language server
	/// and a script's compile alike, so the editor reports what the build does. Codegen then emits Program() and decides
	/// nothing about what is lowered. The steps, each lowering what it reaches (ProgramReach, D-16): every function
	/// written as it is with a body; with `everyVtable`, every class's vtables; reflection's invoke and type thunks; the
	/// globals' starts, round by round; the thunks of what those reached; then what every global's storage needs (its
	/// vtables, the function a function pointer starts at) - again while that lowers more. A step after a failed body
	/// still runs, so one run reports every error.
	class ProgramLowering
	{
	public:
		ProgramLowering(ProgramModel& model, LoweringOptions options);
		ProgramLowering(const ProgramLowering&) = delete; // its parts hold each other
		ProgramLowering& operator=(const ProgramLowering&) = delete;

		/// False when a body or a start failed to lower (Errors() says why).
		bool Run();

		/// The located diagnostics of every lowering, warnings too.
		const ErrorTracker& Errors() const { return m_Errors; }
		LoweredProgram& Program() { return m_Program; }
		const LoweringStats& Stats() const { return m_Lowering.Stats(); }
		/// How long each step took (-ftime).
		const std::vector<std::pair<const char*, double>>& StepSeconds() const { return m_StepSeconds; }
		/// The threads lowering keeps, for codegen to emit on.
		WorkerPool& Pool() { return m_Lowering.Pool(); }

	private:
		void LowerWrittenBodies();
		/// A script's host defines `record` in one of its interface files.
		bool IsHosts(const FunctionRecord& record) const;
		void HoldEveryObjectType();
		void LowerThunks();
		void LowerInvokeThunks();
		void LowerTypeThunks();
		/// What the storage of each global not looked at yet needs. Whether that lowered any function.
		bool LowerGlobalStorage();
		/// The model's changes lowerings made outside a request while they shared it, as internal errors.
		void ReportViolations();
		/// Adds the time from `since` to now to `step`, and moves `since` to now.
		void Timed(const char* step, std::chrono::steady_clock::time_point& since);

		ProgramModel& m_Model;
		LoweringOptions m_Options;
		ErrorTracker m_Errors;
		ParallelLowering m_Lowering;
		LoweredProgram m_Program;
		ProgramReach m_Reach;
		GlobalStarts m_Starts;
		InvokeThunkPlan m_Invokes;
		std::unordered_set<const SymbolInfo*> m_StorageSeen; // globals whose storage's needs are known
		std::vector<std::pair<const char*, double>> m_StepSeconds;
	};
} // namespace C4Compiler
