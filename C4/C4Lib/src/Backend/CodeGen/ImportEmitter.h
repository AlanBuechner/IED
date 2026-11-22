#pragma once
#include "Backend/CodeGen/PartitionSet.h"
#include "Backend/Lowering/LoweredProgram.h"

namespace C4Compiler
{
	class ProgramModel;

	/// Each partition of a split program copies the small functions it calls from the others, `available_externally`,
	/// so they still inline (D-81, ImportPlanner): the facts of each partition's functions are gathered on its thread,
	/// the copies picked from all of them, and each partition emits its copies from their trees. With
	/// `onlyAlwaysInline` (-O0) a partition copies only the `inline` functions it calls.
	void EmitImports(PartitionSet& partitions, const ProgramModel& model, const LoweredProgram& program, bool onlyAlwaysInline, WorkerPool& pool);
} // namespace C4Compiler
