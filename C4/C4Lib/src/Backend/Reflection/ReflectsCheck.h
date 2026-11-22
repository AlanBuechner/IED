#pragma once

namespace C4Compiler
{
	class ErrorTracker;
	class ProgramModel;
	class ReflectionScope;
	struct LoweredProgram;

	/// `[reflects("T")]` (D-89): a function template that reads its type parameter `T` through reflection (a serializer:
	/// `DescribeValue(nameof(T))`). Every instance the program lowered whose `T` is out of reflection's scope, or holds
	/// one - through its fields, array elements and template arguments, as a serializer walks it - is an error at the
	/// type that is out, naming the instance and the path to it. A `[reflects]` naming no type parameter of its function
	/// is an error too.
	void CheckReflectedArguments(const ProgramModel& model, const LoweredProgram& program, const ReflectionScope& scope, ErrorTracker& errors);
}
