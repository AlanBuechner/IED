#pragma once
#include "Backend/Lowering/ProgramReach.h"
#include "Backend/Lowering/ScriptLowering.h"

namespace Scripting
{
	class ProgramModel;

	/// The starts of the program's globals and static members (docs/language/06), in rounds: each round starts the ones
	/// not started yet, in declaration order, and the functions it reaches are lowered before the next, whose round
	/// starts the static data members of the template instances those made. A program's thread_locals start in rounds of
	/// their own when a thread attaches; a script's that run code start on their first use on each thread, and its host's
	/// storage counts as started (its starts ran in the host).
	class GlobalStarts
	{
	public:
		GlobalStarts(ProgramModel& model, ProgramReach& reach, ErrorTracker& errors, const ScriptLowering* script)
			: m_Model(model), m_Reach(reach), m_Errors(errors), m_Script(script) {}

		/// Rounds until no global is left to start. Whether it lowered any start.
		bool Lower();

	private:
		/// Lowers the next round of `kind` when a global of its kind is left to start; a global whose start is constant
		/// data goes with the round for codegen to start as data.
		bool LowerRound(UnitKind kind);
		bool LowerStartsOnFirstUse();
		void MarkHostStorageStarted();

		ProgramModel& m_Model;
		ProgramReach& m_Reach;
		ErrorTracker& m_Errors;
		const ScriptLowering* m_Script;
		StartedGlobals m_Started;
		bool m_Checked = false; // the constant starts were checked
	};
} // namespace Scripting
