#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/ProgramModel.h"

namespace Scripting
{
	/// The first place `user`'s tree (its nested functions too) calls `callee` or takes its address; a location
	/// without a line when there is none.
	SourceLocation UseSite(const HIRFunction& user, const FunctionRecord& callee);

	/// What an error inside a template-made body adds, so that it names the instance and where the program uses it:
	/// " (in 'Core::Array<Net::TcpStream>::Push', used at main.cf (12:5))"; the instance alone when no code of `user`
	/// (null: the program itself, through a global's storage or every vtable of a script host) names it at a line. Empty
	/// for a function not made from a template.
	Core::String UseNote(const ProgramModel& model, const FunctionRecord& made, const HIRFunction* user);
}
