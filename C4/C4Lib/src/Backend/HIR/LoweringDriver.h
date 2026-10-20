#pragma once
#include "Model/ProgramModel.h"
#include "Compiler/ErrorTracker.h"
#include "Backend/HIR/HIRNodes.h"

namespace Scripting
{
	// =====================================================================
	// LOWERING DRIVER - one function at a time. LowerFunction sets up the transient context, pushes
	// the function scope, binds parameters as locals (names from the record), and - for an
	// instantiated template method - registers its FAKE ALIAS bindings so `T`/`N` resolve through
	// ordinary name lookup (no AST rewriting). Every layer is popped by RAII (even on error) so
	// nothing leaks into the next lowering. Returns the HIRFunction BY VALUE (moved): the caller
	// owns it and its arena. A function that fails to lower returns an incomplete tree
	// (complete == false) with diagnostics already in the tracker; codegen skips trees not complete.
	// =====================================================================
	HIRFunction LowerFunction(ProgramModel& model, ErrorTracker& errors, const FunctionRecord* record);
} // namespace Scripting
