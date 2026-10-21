#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Model/Literals.h"
#include "Model/Types.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Scripting
{
	// Literals: numbers typed by Model/Literals (C's rules), characters and strings by their code-unit width,
	// `true` / `false`, and `null` (a void* zero that converts to any pointer at its use).

	QueryResult<Value> ExprLowerer::ResolveLiteral(const LiteralNode* n)
	{
		TypeStore* ts = m_Ctx.queries().TypeStore();
		switch (n->GetKind())
		{
		case LiteralNode::LiteralKind::Number:
			return NumberConstant(n);
		case LiteralNode::LiteralKind::Boolean:
			return Constant(ts->Builtin(TypeKind::Bool), ConstantValue::Bool(n->GetValue() == "true"));
		case LiteralNode::LiteralKind::Char:
			return Constant(ts->Builtin(CharacterType(n->GetUnitWidth())), ConstantValue::Int(CharacterValue(n->GetValue(), n->GetUnitWidth())));
		case LiteralNode::LiteralKind::String:
			return Constant(ts->Pointer(ts->Builtin(CharacterType(n->GetUnitWidth()))), ConstantValue::String(n->GetValue(), n->GetUnitWidth()));
		case LiteralNode::LiteralKind::Null:
			return Constant(ts->Pointer(ts->Builtin(TypeKind::Void)), ConstantValue::Int(0));
		}
		return QueryResult<Value>::Fail(QueryError::NotAValue, "literal has no value type");
	}

	QueryResult<Value> ExprLowerer::NumberConstant(const LiteralNode* n)
	{
		auto literal = ParseNumericLiteral(n->GetValue());
		if (!literal.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(literal.error, literal.message));
		const Type* type = m_Ctx.queries().TypeStore()->Builtin(literal.value.type);
		return Constant(type, literal.value.IsFloat() ? ConstantValue::Float(literal.value.real)
			: ConstantValue::Int(static_cast<int64>(literal.value.bits)));
	}

	QueryResult<Value> ExprLowerer::ResolveTextAs(const LiteralNode* text, const ArrayType* array)
	{
		using Result = QueryResult<Value>;
		const uint8 width = text->GetUnitWidth();
		const Type* unit = m_Ctx.queries().TypeStore()->Builtin(CharacterType(width));
		if (TypeHandle::UnwrapToValue(array->GetElementType()) != unit)
			return m_Ctx.Fail(text, Result::Fail(QueryError::NotAValue, "a string of '" + unit->GetName() + "' units starts only a '" +
				unit->GetName() + "' array, not '" + array->GetName() + "'"));
		const Core::String& units = text->GetValue();
		const uint64 count = units.size() / width;
		if (count > array->GetElementCount())
			return m_Ctx.Fail(text, Result::Fail(QueryError::NotAValue, "the string has " + std::to_string(count) + " characters: more than '" +
				array->GetName() + "' holds"));
		const Type* indexType = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
		auto filled = Lifecycle(m_Ctx).BuildTemporary(array, "__text", [&](BlockNode* steps, const LValue* temp) {
			Value whole; whole.isLValue = true; whole.lvalue = temp; whole.type = array;
			for (uint64 i = 0; i < count; ++i)
			{
				const HIRNode* index = m_Ctx.out->NewNode<ConstantNode>(indexType, ConstantValue::Int((int64)i));
				const HIRNode* value = m_Ctx.out->NewNode<ConstantNode>(unit, ConstantValue::Int(CharacterValue(units.substr(i * width, width), width)));
				steps->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(BuildIndexLValue(m_Ctx, whole, index, unit, unit->GetSize()), value));
			}
			return QueryResult<void>::Success();
		});
		if (!filled.Ok()) return Result::Fail(filled.error, filled.message);
		return Value::R(filled.value, array);
	}

	Value ExprLowerer::Constant(const Type* type, const ConstantValue& value)
	{
		return Value::R(m_Ctx.out->NewNode<ConstantNode>(type, value), type);
	}
}
