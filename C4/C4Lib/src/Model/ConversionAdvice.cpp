#include "Model/ConversionAdvice.h"
#include "Model/OverloadResolution.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"

namespace Scripting
{
	namespace
	{
		bool Gives(const ObjectType::MethodInfo& method, const ObjectType* to)
		{
			return !method.m_IsStatic && !method.m_IsExtensionMethod && method.m_Access == AccessModifier::Public &&
				method.m_ParameterTypes.Count() == 0 && TypeHandle::UnwrapToValue(method.m_ReturnType) == to;
		}

		Core::String FromMethod(const ObjectType* from, const ObjectType* to)
		{
			for (const ObjectType::MethodInfo& method : from->GetMethods())
			{
				if (!Gives(method, to)) continue;
				if (method.HasOperator("cast")) return ": convert it with (" + QualifiedName(to) + ")x";
				if (method.m_OperatorAnnotations.Count() == 0) return ": pass x." + method.mName + "()";
			}
			return "";
		}

		bool CreatesFrom(const ObjectType* to, const Type* from)
		{
			const Core::Vector<uint32>* creates = to->MethodsWithOperator("create");
			if (!creates) return false;
			for (uint32 i : *creates)
			{
				const ObjectType::MethodInfo& create = to->GetMethods()[i];
				if (create.m_ParameterTypes.Count() == 1 && OverloadResolver::ParamCost(create.m_ParameterTypes[0], from) >= 0) return true;
			}
			return false;
		}
	}

	Core::String ConversionAdvice(const Type* from, const Type* to)
	{
		const auto* target = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(to));
		if (!target || !from) return "";
		if (const auto* source = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(from)))
			if (Core::String advice = FromMethod(source, target); !advice.empty()) return advice;
		return CreatesFrom(target, from) ? ": construct one with " + QualifiedName(target) + "(...)" : "";
	}
}
