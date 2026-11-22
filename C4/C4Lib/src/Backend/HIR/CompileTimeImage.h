#pragma once
#include "Backend/HIR/ConstantImage.h"

namespace C4Compiler
{
	class ProgramModel;
	struct AstNode;
	struct Scope;

	/// D-91: the data a global starts holding when its initializer is a call of a `[compile_time]` function returning plain
	/// data - no pointer, no vtable, nothing to destroy (a lookup table, a struct of numbers): the call runs while the
	/// program compiles and what it returns is the global's image. Nothing when the initializer is no such call or the
	/// call cannot run; the global then starts by running its initializer before main, as any other.
	std::optional<ConstantData> CompileTimeImage(ProgramModel& model, const SymbolInfo& global, const AstNode* initializer, const Scope* scope,
		uint64 size);
} // namespace C4Compiler
