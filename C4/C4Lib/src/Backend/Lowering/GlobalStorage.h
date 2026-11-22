#pragma once
#include "Model/SymbolTable.h"
#include <vector>

namespace C4Compiler
{
	class ProgramModel;

	/// The globals and static members that have storage of their own: every variable of the program but one bound to a
	/// foreign symbol (`[link("sym")]`) and a constant with no symbol (an unscoped enum's member). Reflection registers
	/// each, so each has its storage whether or not code names it.
	std::vector<const SymbolInfo*> GlobalsWithStorage(const ProgramModel& model);

	/// The function a function-pointer global starts at when its initializer names one (`funcptr<R(A)> g = F;`, or a
	/// qualified name): the overload of the global's signature, as its unit sees the name; null for any other global.
	const FunctionRecord* FunctionPointerStart(ProgramModel& model, const SymbolInfo& global);
} // namespace C4Compiler
