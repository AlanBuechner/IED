#pragma once
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Lowering/ScriptLowering.h"
#include "Backend/Reflection/ReflectionScope.h"
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace C4Compiler
{
	class ParallelLowering;
	class ProgramModel;

	/// How reflection calls each function it registers (D-50): through a thunk of its own, or the one every function of
	/// its signature shares. Planned round by round as lowering makes more functions callable.
	class InvokeThunkPlan
	{
	public:
		/// A thunk to lower: its record, the target it is lowered from, and whether it is its signature's shared one.
		struct Job
		{
			const FunctionRecord* target = nullptr;
			FunctionRecord* thunk = nullptr;
			bool shared = false;
		};

		/// A script's host has thunks of its own functions.
		void ForScript(const ScriptLowering* script) { m_Script = script; }

		/// The reflected functions reflection can call that have code now and were not planned yet, in the order of
		/// their records: a body lowered completely (`defined`), or a virtual method (its thunk dispatches through the
		/// object, so the method is needed: docs/runtime/02 section 3.2).
		std::vector<const FunctionRecord*> NewTargets(const ProgramModel& model, const std::function<bool(const FunctionRecord&)>& defined);
		/// The thunks `targets` need, in their order: each its own, or the one its signature shares, once; their records
		/// added to `records`. What each target wants is worked out on lowering's threads.
		std::vector<Job> Plan(ProgramModel& model, const std::vector<const FunctionRecord*>& targets, ParallelLowering& lowering,
			std::deque<FunctionRecord>& records);
		/// Which of the last plan's jobs lowered completely (`complete[i]`): each of its targets that has a thunk then,
		/// with that thunk. A function whose thunk did not lower is one reflection cannot call.
		std::vector<InvokeThunkUse> Settle(const std::vector<Job>& jobs, const std::vector<bool>& complete);

	private:
		/// The thunk a target calls through: its signature's shared one or its own, and its symbol.
		struct Wanted
		{
			bool shared = false;
			std::string symbol;
		};

		bool HasCallableCode(const ProgramModel& model, const FunctionRecord& record, const std::function<bool(const FunctionRecord&)>& defined) const;

		const ScriptLowering* m_Script = nullptr;
		std::optional<ReflectionScope> m_Scope;                     // which functions reflection registers (D-89)
		uint32 m_RecordsScanned = 0;                                // the model's records looked at so far
		std::vector<const FunctionRecord*> m_Candidates;            // reflected and callable, without code to call yet
		std::unordered_set<const FunctionRecord*> m_Planned;        // considered for a thunk (one that failed too: no retry)
		std::unordered_map<std::string, const FunctionRecord*> m_SharedThunks; // by symbol; null for a signature that cannot be passed
		std::vector<const FunctionRecord*> m_Targets;               // the last plan's targets
		std::vector<Wanted> m_Wanted;                                // what each of them wants
	};
} // namespace C4Compiler
