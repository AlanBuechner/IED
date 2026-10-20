#include "Model/CompileTimeQueries.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h"
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

	QueryResult<int64> OffsetOfField(const Type* type, const Core::Vector<Token>& path)
	{
		int64 offset = 0;
		const Type* current = type;
		for (uint32 i = 0; i < path.Count(); ++i)
		{
			auto measured = Measured(current);
			if (!measured.Ok()) return QueryResult<int64>::Fail(measured.error, measured.message);
			const auto* object = dynamic_cast<const ObjectType*>(measured.value);
			const Core::String& name = path[i].GetText();
			if (!object)
				return QueryResult<int64>::Fail(QueryError::NotAValue,
					"offsetof: '" + measured.value->GetName() + "' has no fields, so it has no '" + name + "'");
			const ObjectType::FieldInfo* field = object->GetFieldByName(name);
			if (!field)
				return QueryResult<int64>::Fail(QueryError::NotFound, "offsetof: '" + object->GetName() + "' has no field '" + name + "'");
			if (field->m_IsStatic)
				return QueryResult<int64>::Fail(QueryError::NotAValue,
					"offsetof: '" + name + "' is a static member of '" + object->GetName() + "', which has no offset");
			offset += object->GetFieldOffset(name);
			current = field->m_Type;
		}
		return QueryResult<int64>::Success(offset);
	}

	QueryResult<int64> CompileTimeQueryValue(const TypeOfExpressionNode& query, const Type* operandType)
	{
		switch (query.GetKind())
		{
		case TypeOfExpressionNode::Kind::SizeOf: return SizeOfType(operandType);
		case TypeOfExpressionNode::Kind::AlignOf: return AlignOfType(operandType);
		case TypeOfExpressionNode::Kind::OffsetOf: return OffsetOfField(operandType, query.GetFieldPath());
		default: return QueryResult<int64>::Fail(QueryError::NotAValue, "'" + query.ToString() + "' is not a constant");
		}
	}
}
