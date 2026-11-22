#pragma once
#include "Backend/Lowering/LoweredProgram.h"
#include "Backend/Lowering/ParallelLowering.h"
#include "Backend/Lowering/VtableEntries.h"
#include "Compiler/ErrorTracker.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace C4Compiler
{
	class ProgramModel;

	/// The functions a program needs, lowered as they are found (D-16): a function is lowered when something lowered
	/// before it names it - code calls it, takes its address, constructs, finalizes, destroys at exit or copies and
	/// destroys an exception with it - or when an object the code holds has a vtable that points at it. A body is kept
	/// with its diagnostics, a template-made one's ending with where the program first uses it; what it needs is lowered
	/// next. The order everything is lowered in follows from the order it is found, never from the threads.
	class ProgramReach
	{
	public:
		ProgramReach(ProgramModel& model, ParallelLowering& lowering, LoweredProgram& program, ErrorTracker& errors);

		/// `record` is needed, first by `user` (null: by the program itself).
		void Need(const FunctionRecord* record, const HIRFunction* user);
		/// The program holds an object of `type`: what its vtables point at is needed.
		void Holds(const Type* type, const HIRFunction* user);
		/// Complete trees that are not a function's body join the program (thunks, a round of starts), in order: what they
		/// need is lowered with the rest.
		void Keep(std::vector<LoweredUnit> units);
		/// Lowers every needed function not lowered yet, and what those need, until nothing new is needed. Whether it
		/// lowered any.
		bool LowerNeeded();

		/// `record`'s body lowered completely: the program has its code.
		bool Defined(const FunctionRecord& record) const { return m_Defined.count(&record) != 0; }

	private:
		/// The needed functions with a body of their own not lowered yet (a nested one comes with its parent).
		std::vector<const FunctionRecord*> TakeBatch();
		/// What each tree needs, worked out on the lowering threads (each reads its own tree).
		std::vector<BodyNeeds> NeedsOfEach(const std::vector<const HIRFunction*>& trees);
		void KeepBody(LoweredBody body, const BodyNeeds& needs);
		void NeedWhatItNeeds(const HIRFunction& user, const BodyNeeds& needs);
		void NeedCensus(const CensusNeed& need, const HIRFunction* user);
		void LowerCensusNeeded();
		/// A template-made body's diagnostics name the instance and where the program first uses it.
		void NoteUse(const FunctionRecord& record, ErrorTracker& errors) const;

		ProgramModel& m_Model;
		ParallelLowering& m_Lowering;
		LoweredProgram& m_Program;
		ErrorTracker& m_Errors;
		VtableEntries m_Vtables;
		std::vector<const FunctionRecord*> m_Pending;                             // needed, in the order found
		std::unordered_set<const FunctionRecord*> m_Needed;
		std::unordered_map<const FunctionRecord*, const HIRFunction*> m_FirstUsers; // the body that first named each
		std::unordered_set<const Type*> m_Held;                                    // types whose vtables are needed
		std::vector<CensusNeed> m_PendingCensus;
		std::unordered_set<const FunctionRecord*> m_Defined;
	};
} // namespace C4Compiler
