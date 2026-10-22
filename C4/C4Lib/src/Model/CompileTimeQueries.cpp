#include "Model/CompileTimeQueries.h"
#include "Model/TypeHandle.h"
#include "Model/Types/NativeMemory.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"

namespace Scripting
{
	namespace
	{
		// The value a query measures: a reference's referent, laid out now (a query in a field's extent runs
		// before the end-of-pass layout sweep).
		QueryResult<const Type*> Measured(const Type* type)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			if (!value) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "the operand has no type");
			if (value->GetKind() == TypeKind::Void) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "'void' has no size");
			if (const auto* object = dynamic_cast<const ObjectType*>(value))
			{
				const_cast<ObjectType*>(object)->EnsureLayout();
				if (!object->IsDefined())
					return QueryResult<const Type*>::Fail(QueryError::NotAValue, "sizeof of incomplete type '" + value->GetName() + "'");
			}
			return QueryResult<const Type*>::Success(value);
		}
	}

	QueryResult<int64> SizeOfType(const Type* type)
	{
		auto measured = Measured(type);
		if (!measured.Ok()) return QueryResult<int64>::Fail(measured.error, measured.message);
		return QueryResult<int64>::Success(static_cast<int64>(measured.value->GetSize()));
	}

	QueryResult<int64> AlignOfType(const Type* type)
	{
		auto measured = Measured(type);
		if (!measured.Ok()) return QueryResult<int64>::Fail(measured.error, measured.message);
		return QueryResult<int64>::Success(static_cast<int64>(measured.value->GetAlignment()));
	}

	namespace
	{
		struct FieldStep
		{
			int64 offset = 0;
			const Type* type = nullptr;
		};

		// One name of offsetof's path: where it is in an object or a union (BUG-286), and its type.
		QueryResult<FieldStep> StepInto(const Type* container, const Core::String& name)
		{
			if (const auto* object = dynamic_cast<const ObjectType*>(container))
			{
				const ObjectType::FieldInfo* field = object->GetFieldByName(name);
				if (!field)
					return QueryResult<FieldStep>::Fail(QueryError::NotFound, "offsetof: '" + object->GetName() + "' has no field '" + name + "'");
				if (field->m_IsStatic)
					return QueryResult<FieldStep>::Fail(QueryError::NotAValue,
						"offsetof: '" + name + "' is a static member of '" + object->GetName() + "', which has no offset");
				return QueryResult<FieldStep>::Success({ object->GetFieldOffset(name), field->m_Type });
			}
			if (const auto* un = dynamic_cast<const UnionType*>(container))
			{
				for (const ObjectType::FieldInfo& field : un->GetFields())
					if (field.mName == name) return QueryResult<FieldStep>::Success({ un->GetFieldOffset(name), field.m_Type });
				return QueryResult<FieldStep>::Fail(QueryError::NotFound, "offsetof: '" + un->GetName() + "' has no field '" + name + "'");
			}
			return QueryResult<FieldStep>::Fail(QueryError::NotAValue, "offsetof: '" + container->GetName() + "' has no fields, so it has no '" + name + "'");
		}
	}

	QueryResult<int64> OffsetOfField(const Type* type, const Core::Vector<Token>& path)
	{
		int64 offset = 0;
		const Type* current = type;
		for (uint32 i = 0; i < path.Count(); ++i)
		{
			auto measured = Measured(current);
			if (!measured.Ok()) return QueryResult<int64>::Fail(measured.error, measured.message);
			auto step = StepInto(measured.value, path[i].GetText());
			if (!step.Ok()) return QueryResult<int64>::Fail(step.error, step.message);
			offset += step.value.offset;
			current = step.value.type;
		}
		return QueryResult<int64>::Success(offset);
	}

	namespace
	{
		QueryResult<ConstantValue> Measure(const QueryResult<int64>& measured)
		{
			if (!measured.Ok()) return QueryResult<ConstantValue>::Fail(measured.error, measured.message);
			return QueryResult<ConstantValue>::Success(ConstantValue::Int(measured.value));
		}
	}

	QueryResult<ConstantValue> CompileTimeQueryValue(const TypeOfExpressionNode& query, const Type* operandType)
	{
		switch (query.GetKind())
		{
		case TypeOfExpressionNode::Kind::SizeOf: return Measure(SizeOfType(operandType));
		case TypeOfExpressionNode::Kind::AlignOf: return Measure(AlignOfType(operandType));
		case TypeOfExpressionNode::Kind::OffsetOf: return Measure(OffsetOfField(operandType, query.GetFieldPath()));
		case TypeOfExpressionNode::Kind::HoldsNativeBytes:
			return QueryResult<ConstantValue>::Success(ConstantValue::Bool(NativeMemory::Holds(TypeHandle::UnwrapToValue(operandType))));
		default: return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + query.ToString() + "' is not a constant");
		}
	}
}
