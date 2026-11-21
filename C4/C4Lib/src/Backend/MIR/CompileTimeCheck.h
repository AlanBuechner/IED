#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/CompileTimeMark.h"
#include "Model/QueryResult.h"

namespace Scripting
{
	/// Whether compile-time evaluation may run `record`'s body: it is marked, or the compiler wrote it (a create, copy or
	/// delete runs when what it calls can).
	bool MayRunAtCompileTime(const FunctionRecord& record);
	/// What a function is called in compile-time evaluation's messages.
	Core::String CompileTimeName(const FunctionRecord& record);

	/// D-91, where a `[compile_time]` function is defined: it may do only what compile time can run - no heap, no assembly,
	/// no static locals or exit registrations, no global variable that is not a constant, no call through a pointer, a func
	/// value or a vtable, no call of a function that is not marked itself - and the mark takes no arguments. Its first
	/// breach, or success. Only what runs while nothing is thrown counts: an exception ends the evaluation.
	QueryResult<void> CheckCompileTimeBody(const HIRFunction& fn);
} // namespace Scripting
