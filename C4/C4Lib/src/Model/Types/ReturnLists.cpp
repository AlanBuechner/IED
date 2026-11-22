#include "Model/Types/ReturnLists.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include <string>

namespace Scripting
{
	Core::String ReturnLists::FieldName(uint32 index)
	{
		return "_" + std::to_string(index);
	}

	QueryResult<const ObjectType*> ReturnLists::Of(const ProgramModel& model, const std::vector<const Type*>& elements)
	{
		using Result = QueryResult<const ObjectType*>;
		if (const ObjectType* const* cached = model.m_ReturnLists.Find(elements)) return *cached;
		const ModelMaking::Request request(model.Making(), "a return list");
		if (const ObjectType* const* cached = model.m_ReturnLists.Find(elements)) return *cached; // made meanwhile
		Core::String name = "[";
		for (size_t i = 0; i < elements.size(); ++i)
		{
			const Type* t = elements[i];
			if (!t || t->GetKind() == TypeKind::Reference || t->GetKind() == TypeKind::Void)
				return Result::Fail(QueryError::NotAValue, "a return list holds values: '" + (t ? t->GetName() : Core::String("?")) +
					"' cannot be one of them");
			name += (i ? ", " : "") + t->GetName();
		}
		auto* type = new ObjectType(TypeKind::Object, name + "]");
		type->SetObjectKind(ObjectType::ObjectKind::Struct);
		type->SetDefined(true);
		type->SetReturnList(true);
		for (uint32 i = 0; i < elements.size(); ++i)
		{
			ObjectType::FieldInfo field;
			field.mName = FieldName(i);
			field.m_Type = elements[i];
			type->AddField(field);
		}
		type->CalculateLayout();
		type->MarkLaidOut(); // read by every lowering once shown: never laid out again
		model.m_Types->Own(type);
		model.KeepObjectType(type);
		model.m_ReturnLists.Add(elements, type);
		return Result(static_cast<const ObjectType*>(type));
	}
} // namespace Scripting
