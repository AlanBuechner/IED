#include "Backend/HIR/CompileTimeImage.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/ModelQueries.h"
#include "Model/ConstEvaluator.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"

namespace Scripting
{
	namespace
	{
		// A value of `type` holds a vtable pointer somewhere, which the evaluator's objects do not have.
		bool HoldsVtable(const Type* type)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			if (const auto* array = dynamic_cast<const ArrayType*>(value)) return HoldsVtable(array->GetElementType());
			if (const auto* both = dynamic_cast<const UnionType*>(value))
			{
				for (const auto& field : both->GetFields())
					if (HoldsVtable(field.m_Type)) return true;
				return false;
			}
			const auto* object = dynamic_cast<const ObjectType*>(value);
			if (!object) return false;
			if (object->IsPolymorphic()) return true;
			for (const ObjectType* base : object->GetBaseTypes())
				if (HoldsVtable(base)) return true;
			for (const auto& field : object->GetFields())
				if (!field.m_IsStatic && HoldsVtable(field.m_Type)) return true;
			return false;
		}

		bool PlainData(ProgramModel& model, const Type* type)
		{
			return type && !TypeHandle::MayHoldPointers(type) && !HoldsVtable(type) && !LifecycleFacts::NeedsDestroy(ModelQueries(&model), type);
		}
	}

	std::optional<ConstantData> CompileTimeImage(ProgramModel& model, const SymbolInfo& global, const AstNode* initializer, const Scope* scope,
		uint64 size)
	{
		if (!initializer || !model.CompileTimeCalls() || !PlainData(model, global.type)) return std::nullopt;
		QueryResult<std::vector<uint8>> bytes = ConstEvaluator(model, scope).EvaluateData(initializer, global.definingUnit);
		if (!bytes.Ok() || bytes.value.size() != size) return std::nullopt;
		ConstantData data;
		data.bytes = std::move(bytes.value);
		return data;
	}
} // namespace Scripting
