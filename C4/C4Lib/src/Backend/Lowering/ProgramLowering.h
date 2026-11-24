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
#include <string>
#include <unordered_map>
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

		/// A build server's body edit (docs/compiler/07 stage 2a): lowers `records` again from the bodies they hold now, in
		/// place of the trees Run lowered, when the program is then what a cold build of the edit lowers - each body lowers
		/// without errors, needs what it needed, and asks the model for nothing it had not made by the old body's pass and
		/// for everything only it asked for in that pass. Empty then; otherwise why not, and the program is half changed: a
		/// cold build follows.
		std::string Relower(const std::vector<const FunctionRecord*>& records);

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
		/// A function lowered whether or not anything names it: written as it is, with a body of its own (LowerWrittenBodies).
		bool LoweredFirst(const FunctionRecord& record) const;
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
		/// Relower: each record's unit in Program(), or why one has none.
		std::string FindUnits(const std::vector<const FunctionRecord*>& records, std::vector<size_t>& units) const;
		/// Relower: why a body lowered again does not stand for the old one as a cold build would lower it; empty when it does.
		std::string Differs(const LoweredBody& after) const;
		/// Relower: what the first pass's bodies, kept in order, make the reach do (KeepBody's Need, Holds and NeedCensus) -
		/// the functions needed next, a held type's vtables' included, with the body that first needed each, and the census
		/// counters - with the units `swapped` names needing what it gives. A cold build of the edit lowers what the build
		/// before lowered after that pass when this is the same: holding a type does nothing but need its vtables'
		/// functions.
		struct FirstPassReach
		{
			std::vector<std::pair<const FunctionRecord*, const FunctionRecord*>> needed;
			std::vector<CensusNeed> census;
			bool operator==(const FirstPassReach&) const = default;
		};
		FirstPassReach ReplayFirstPass(const std::unordered_map<size_t, const BodyNeeds*>& swapped);
		/// Relower: why the bodies' asks would change what the model makes in a cold build; empty when they change nothing.
		std::string AsksDiffer(const std::vector<size_t>& units, const std::vector<LoweredBody>& bodies) const;
		/// Relower: why a body some swapped tree first named would report other diagnostics now; empty when none would.
		std::string FirstUsesDiffer(const std::vector<size_t>& units) const;
		/// Relower: `after` in place of the unit `unit`, its diagnostics among the program's where the old one's were.
		void Replace(size_t unit, LoweredBody after, BodyNeeds needs);
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
