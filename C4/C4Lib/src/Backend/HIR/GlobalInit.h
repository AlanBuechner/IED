#pragma once
#include "Backend/HIR/HIRNodes.h"
#include <memory>
#include <unordered_set>
#include <vector>

namespace Scripting
{
	class ProgramModel;
	class ErrorTracker;

	/// The globals whose starts are already lowered.
	using StartedGlobals = std::unordered_set<const SymbolInfo*>;

	/// One round of the global initialization that runs before main (docs/language/06): every global and static
	/// member not in `started` that does not start as a constant starts here - from its initializer, else by its
	/// default construction - in declaration order (units in build order, each top to bottom), and one that
	/// needs destruction is registered to be destroyed at exit, in the reverse order. A template instance that
	/// code reached from a round makes has its static data members started by a later round. False when no
	/// global was left to start; failures go to `errors`. thread_local ones are LowerThreadInit's.
	bool LowerGlobalInit(ProgramModel& model, ErrorTracker& errors, HIRFunction& out, StartedGlobals& started);

	/// The same for the thread_local globals and static members: a round of the starts each thread runs on its own
	/// copies when it attaches (D-48), each registered to be destroyed when that thread ends.
	bool LowerThreadInit(ProgramModel& model, ErrorTracker& errors, HIRFunction& out, StartedGlobals& started);

	/// A global or static member whose start runs code: an initializer that does not fold, or a type that is
	/// constructed or destroyed.
	bool StartsWithCode(ProgramModel& model, const SymbolInfo& sym);

	/// A script's thread_local whose start runs code (docs/runtime/04 section 2) and the body of the function that
	/// starts it: once on the calling thread, the first time it runs there.
	struct FirstUseStart
	{
		const SymbolInfo* variable = nullptr;
		std::unique_ptr<HIRFunction> function;
	};

	/// The thread_local globals and static members not in `started` that do not start as a constant, each started on
	/// its first use on each thread instead of when the thread attaches; a start that throws leaves the storage zero
	/// for the next use to try again. Failures go to `errors`.
	std::vector<FirstUseStart> LowerFirstUseStarts(ProgramModel& model, ErrorTracker& errors, StartedGlobals& started);

	/// A global or static member that starts as its folded constant passes the checks its start would at run time
	/// (the conversion to its type, its warnings); failures go to `errors`.
	void CheckConstantStarts(ProgramModel& model, ErrorTracker& errors);

} // namespace Scripting
