#include "Model/CompileTimeQueries.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/NativeMemory.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"
#include "Model/Types/ArrayType.h"
#include <unordered_set>
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"

namespace Scripting
{
	namespace
	{
		using Visited = std::unordered_set<const Type*>;

		bool PartsSettled(const Core::Vector<ObjectType::FieldInfo>& fields, Visited& visited);

		// Whether every part of a value of `type` is known: each object it holds by value (itself, its bases, its fields'
		// objects, its elements) is defined and has no field or base the resolver has yet to attach (BUG-393). A query
		// that runs while the model resolves (an extent, a constant) waits for them rather than measure a shell.
		bool Settled(const Type* type, Visited& visited)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			if (!value) return false;
			if (!visited.insert(value).second) return true; // a by-value cycle is the layout's error to report
			if (const auto* array = dynamic_cast<const ArrayType*>(value)) return Settled(array->GetElementType(), visited);
			if (const auto* un = dynamic_cast<const UnionType*>(value)) return !un->HasPendingFields() && PartsSettled(un->GetFields(), visited);
			const auto* object = dynamic_cast<const ObjectType*>(value);
			if (!object) return true;
			if (!object->IsDefined() || object->HasPendingParts()) return false;
			for (const ObjectType* base : object->GetBaseTypes())
				if (!Settled(base, visited)) return false;
			return PartsSettled(object->GetFields(), visited);
		}

		bool PartsSettled(const Core::Vector<ObjectType::FieldInfo>& fields, Visited& visited)
		{
			for (const ObjectType::FieldInfo& field : fields)
				if (!field.m_IsStatic && !Settled(field.m_Type, visited)) return false;
			return true;
		}

		bool Settled(const Type* type)
		{
			Visited visited;
			return Settled(type, visited);
		}

		Core::String IncompleteMessage(const char* query, const Type* type)
		{
			return Core::String(query) + " of incomplete type '" + (type ? type->GetName() : Core::String("?")) + "'";
		}

		// The value a query measures: a reference's referent, laid out now (a query in a field's extent runs
		// before the end-of-pass layout sweep).
		QueryResult<const Type*> Measured(const Type* type)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			if (!value) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "the operand has no type");
			if (value->GetKind() == TypeKind::Void) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "'void' has no size");
			if (!Settled(value)) return QueryResult<const Type*>::Fail(QueryError::NotAValue, IncompleteMessage("sizeof", value));
			if (const auto* object = dynamic_cast<const ObjectType*>(value)) const_cast<ObjectType*>(object)->EnsureLayout();
			return QueryResult<const Type*>::Success(value);
		}

		// What a holds_* query reads: the type as written, once its parts are known.
		QueryResult<ConstantValue> Holding(const char* query, const Type* type, bool (*holds)(const Type*))
		{
			if (!type) return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "the operand has no type");
			if (!Settled(type)) return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, IncompleteMessage(query, TypeHandle::UnwrapToValue(type)));
			return QueryResult<ConstantValue>::Success(ConstantValue::Bool(holds(type)));
		}

		bool HoldsNative(const Type* type) { return NativeMemory::Holds(TypeHandle::UnwrapToValue(type)); }
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
		case TypeOfExpressionNode::Kind::HoldsNativeBytes: return Holding("holds_native_bytes", operandType, HoldsNative);
		case TypeOfExpressionNode::Kind::HoldsPointers: return Holding("holds_pointers", operandType, TypeHandle::MayHoldPointers);
		default: return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + query.ToString() + "' is not a constant");
		}
	}

	bool CallsProgramFunction(const ProgramModel& model, const TypeOfExpressionNode& query, uint32 unitId, const Scope* scope)
	{
		if (!query.GetCall()) return false;
		auto symbol = model.QuerySymbol(query.GetKeywordToken().GetText(), unitId, scope);
		return symbol.Ok() && symbol.value && symbol.value->kind == SymbolKind::Function;
	}
}
