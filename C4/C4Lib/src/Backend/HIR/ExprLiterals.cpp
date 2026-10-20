#include "Backend/HIR/ExprLowerer.h"
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

	Value ExprLowerer::Constant(const Type* type, const ConstantValue& value)
	{
		return Value::R(m_Ctx.out->NewNode<ConstantNode>(type, value), type);
	}
}
