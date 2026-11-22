#include "Model/ReflectedEntities.h"
#include "Model/ProgramModel.h"
#include "Model/Types/ObjectType.h"

namespace C4Compiler
{
	bool ReflectedFunction(const FunctionRecord& function)
	{
		return function.type && !function.isLocal && !function.isBuiltin && function.synthesized == LifecycleOp::None &&
			!function.mangledName.empty() && function.mangledName.rfind("c4_", 0) != 0;
	}

	bool ReflectedType(const ProgramModel& model, const ObjectType* object)
	{
		return object && object->IsDefined() && !model.ClosureOf(object) && !object->IsReturnList() && !object->GetName().empty();
	}
} // namespace C4Compiler
