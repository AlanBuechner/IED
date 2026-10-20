#include "Model/ConstEvaluator.h"
#include "Model/CompileTimeQueries.h"
#include "Model/Literals.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types.h"
#include "Parser/Nodes/Expressions/ArrayIndexNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"

namespace Scripting
{
	// sizeof / alignof / offsetof in a constant (an array extent, a const initializer): the operand is a type,
	// or an expression the model can type without a function body around it.

	QueryResult<ConstantValue> ConstEvaluator::FoldQuery(const TypeOfExpressionNode& query, uint32 unitId, Scope* bindingScope) const
	{
		if (query.GetKind() == TypeOfExpressionNode::Kind::SizeOfPack)
			return PackLength(query.GetPackName().GetText(), bindingScope);
		auto operand = QueryOperandType(query, unitId, bindingScope);
		if (!operand.Ok()) return QueryResult<ConstantValue>::Fail(operand.error, operand.message);
		auto value = CompileTimeQueryValue(query, operand.value);
		if (!value.Ok()) return QueryResult<ConstantValue>::Fail(value.error, value.message);
		return QueryResult<ConstantValue>::Success(ConstantValue::Int(value.value));
	}

	QueryResult<const Type*> ConstEvaluator::QueryOperandType(const TypeOfExpressionNode& query, uint32 unitId, Scope* bindingScope) const
	{
		if (query.GetTargetType())
		{
			auto type = m_Model.ResolveTypeInternal(*query.GetTargetType(), unitId, bindingScope, m_Model.m_Symbols->GetRoot());
			if (type.Ok() || !query.GetOperand()) return type;
		}
		return ExpressionType(query.GetOperand().Get(), unitId);
	}

	// A variable, an element (`a[0]`), a field (`s.x`, `p->x`), a dereference, a literal or a cast.
	QueryResult<const Type*> ConstEvaluator::ExpressionType(const AstNode* expr, uint32 unitId) const
	{
		using Result = QueryResult<const Type*>;
		if (const auto* paren = dynamic_cast<const ParenthesizedExpressionNode*>(expr))
			return ExpressionType(paren->GetExpression().Get(), unitId);
		if (const auto* name = dynamic_cast<const IdentifierExpressionNode*>(expr))
		{
			LookupContext context;
			context.unitId = unitId;
			auto variable = m_Model.QueryVariable(name->GetName(), nullptr, context);
			if (!variable.Ok()) return Result::Fail(variable.error, variable.message);
			return Result::Success(variable.value.type);
		}
		if (const auto* index = dynamic_cast<const ArrayIndexNode*>(expr))
		{
			auto array = ExpressionType(index->GetArrayExpression().Get(), unitId);
			if (!array.Ok()) return array;
			const Type* value = TypeHandle::UnwrapToValue(array.value);
			if (const auto* a = dynamic_cast<const ArrayType*>(value)) return Result::Success(a->GetElementType());
			if (const auto* p = dynamic_cast<const PointerType*>(value)) return Result::Success(p->GetPointeeType());
			return Result::Fail(QueryError::NotAValue, "'" + expr->ToString() + "' indexes a value that is not an array or a pointer");
		}
		if (const auto* member = dynamic_cast<const MemberAccessNode*>(expr))
		{
			auto base = ExpressionType(member->GetBaseObject().Get(), unitId);
			if (!base.Ok()) return base;
			const Type* value = TypeHandle::UnwrapToValue(base.value);
			if (const auto* p = dynamic_cast<const PointerType*>(value); p && member->GetAccessOperatorToken().GetType() == TokenType::Arrow)
				value = TypeHandle::UnwrapToValue(p->GetPointeeType());
			const auto* object = dynamic_cast<const ObjectType*>(value);
			const ObjectType::FieldInfo* field = object ? object->GetFieldByName(member->GetMemberNameToken().GetText()) : nullptr;
			if (!field) return Result::Fail(QueryError::NotFound, "'" + expr->ToString() + "' names no field");
			return Result::Success(field->m_Type);
		}
		if (const auto* unary = dynamic_cast<const UnaryExpressionNode*>(expr); unary && unary->GetOperatorToken().GetType() == TokenType::Star)
		{
			auto pointer = ExpressionType(unary->GetOperand().Get(), unitId);
			if (!pointer.Ok()) return pointer;
			if (const auto* p = dynamic_cast<const PointerType*>(TypeHandle::UnwrapToValue(pointer.value))) return Result::Success(p->GetPointeeType());
			return Result::Fail(QueryError::NotAValue, "'" + expr->ToString() + "' dereferences a value that is not a pointer");
		}
		if (const auto* cast = dynamic_cast<const CastExpressionNode*>(expr))
			return m_Model.ResolveTypeInternal(*cast->GetTargetType(), unitId, nullptr, m_Model.m_Symbols->GetRoot());
		if (const auto* literal = dynamic_cast<const LiteralNode*>(expr))
			return LiteralType(*literal);
		return Result::Fail(QueryError::NotAValue, "'" + expr->ToString() +
			"' in a constant: sizeof and alignof take a type, a variable, an element, a field, a literal or a cast here");
	}

	QueryResult<const Type*> ConstEvaluator::LiteralType(const LiteralNode& literal) const
	{
		TypeStore& types = *m_Model.m_Types;
		switch (literal.GetKind())
		{
		case LiteralNode::LiteralKind::Number:
		{
			auto number = ParseNumericLiteral(literal.GetValue());
			if (!number.Ok()) return QueryResult<const Type*>::Fail(number.error, number.message);
			return QueryResult<const Type*>::Success(types.Builtin(number.value.type));
		}
		case LiteralNode::LiteralKind::Char:
			return QueryResult<const Type*>::Success(types.Builtin(CharacterType(literal.GetUnitWidth())));
		case LiteralNode::LiteralKind::String:
			return QueryResult<const Type*>::Success(types.Pointer(types.Builtin(CharacterType(literal.GetUnitWidth()))));
		case LiteralNode::LiteralKind::Boolean:
			return QueryResult<const Type*>::Success(types.Builtin(TypeKind::Bool));
		case LiteralNode::LiteralKind::Null:
			return QueryResult<const Type*>::Success(types.Pointer(types.Builtin(TypeKind::Void)));
		}
		return QueryResult<const Type*>::Fail(QueryError::NotAValue, "a literal with no type");
	}
}
