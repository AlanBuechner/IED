#include "Model/LifecycleMethods.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	namespace
	{
		bool HasAnnotation(const FunctionRecord* rec, const char* name)
		{
			for (const Core::String& a : rec->operatorAnnotations)
				if (a == name) return true;
			return false;
		}

		// A copy-like method takes exactly one reference (or value) of its own type.
		bool TakesOwnType(const FunctionRecord* rec, const ObjectType* type)
		{
			if (!rec->type || rec->type->GetParameterCount() != 1) return false;
			return TypeHandle::UnwrapToValue(rec->type->GetParameters()[0].m_Type) == type;
		}
	}

	LifecycleMethods FindLifecycleMethods(const ProgramModel& model, const ObjectType* type)
	{
		LifecycleMethods own;
		for (uint32 i = 0; i < type->GetMethodCount(); ++i)
		{
			const FunctionRecord* rec = model.RecordForMethod(type, &type->GetMethods()[i]);
			if (!rec || !HasCode(*rec) || rec->isStatic) continue;
			if (HasAnnotation(rec, "create"))
			{
				own.creates.push_back(rec);
				// The default create takes no arguments: one with no parameters, else one whose every parameter has a default.
				const bool noParameters = rec->type && rec->type->GetParameterCount() == 0;
				if (rec->minArity == 0 && (!own.defaultCreate || noParameters)) own.defaultCreate = rec;
			}
			if (HasAnnotation(rec, "delete") && !own.destroy) own.destroy = rec;
			if (HasAnnotation(rec, "copy") && TakesOwnType(rec, type) && !own.copy) own.copy = rec;
			if ((HasAnnotation(rec, "assign") || HasAnnotation(rec, "=")) && TakesOwnType(rec, type) && !own.assign) own.assign = rec;
		}
		return own;
	}
} // namespace Scripting
