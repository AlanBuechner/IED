#pragma once

namespace Scripting
{
	class ObjectType;
	class ProgramModel;
	struct FunctionRecord;

	/// Whether reflection registers `function`: the program's own functions, not builtins, runtime entry points
	/// (`c4_` names), functions local to a body or functions the compiler wrote (BUG-238).
	bool ReflectedFunction(const FunctionRecord& function);

	/// Whether reflection registers the object type: a defined one with a name, not a closure or a return list.
	bool ReflectedType(const ProgramModel& model, const ObjectType* object);
} // namespace Scripting
