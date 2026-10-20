#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/ProgramModel.h"

namespace Scripting
{
	/// The first place `user`'s tree (its nested functions too) calls `callee` or takes its address; a location
	/// without a line when there is none.
	SourceLocation UseSite(const HIRFunction& user, const FunctionRecord& callee);

	/// What an error inside a template-made body adds, so that it names the instance and where the program uses it:
	/// " (in 'Core::Array<Net::TcpStream>::Push', used at main.cf (12:5))". Empty for a function not made from a
	/// template, or one no lowered code uses.
	Core::String UseNote(const ProgramModel& model, const FunctionRecord& made, const HIRFunction* user);
}
