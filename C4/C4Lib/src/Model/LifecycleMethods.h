#pragma once
#include <vector>

namespace Scripting
{
	class ProgramModel;
	class ObjectType;
	struct FunctionRecord;

	/// The lifecycle methods a type declares itself (extension methods included); null when absent.
	struct LifecycleMethods
	{
		const FunctionRecord* defaultCreate = nullptr; // a 0-argument create with a body (or linked)
		const FunctionRecord* destroy = nullptr;       // [operator("delete")]
		const FunctionRecord* copy = nullptr;          // [operator("copy")] taking one T&
		const FunctionRecord* assign = nullptr;        // [operator("=")] / "assign" taking one T&
		std::vector<const FunctionRecord*> creates;    // every create overload with a body (or linked)
	};

	/// What `type`'s methods say now: ProgramModel::LifecycleMethodsOf remembers it.
	LifecycleMethods FindLifecycleMethods(const ProgramModel& model, const ObjectType* type);
} // namespace Scripting
