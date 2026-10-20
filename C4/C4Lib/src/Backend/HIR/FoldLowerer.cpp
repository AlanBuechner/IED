#include "Backend/HIR/FoldLowerer.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/PackElements.h"
#include "Backend/HIR/ReadOnly.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include <array>

namespace Scripting
{
	namespace
	{
		constexpr std::array<const char*, 17> kBinaryFoldOperators = {
			"+", "-", "*", "/", "%", "**", "&", "|", "^", "<<", ">>", "==", "!=", "<", ">", "<=", ">=" };

		bool IsAssignment(const Core::String& op)
		{
			return !op.empty() && op.back() == '=' && op != "==" && op != "!=" && op != "<=" && op != ">=";
		}

		// A location with an address (a SIMD lane has none).
		bool HasAddress(const LValue* location)
		{
			return location->kind != LValue::Kind::Lane && location->kind != LValue::Kind::Swizzle &&
				location->kind != LValue::Kind::VectorLaneDyn;
		}
	}

	QueryResult<Value> FoldLowerer::Lower(const FoldExpressionNode& n)
	{
		auto operation = OperationOf(n.GetOperatorToken().GetText());
		if (!operation.Ok()) return m_Ctx.Fail(&n, QueryResult<Value>::Fail(operation.error, operation.message));
		auto shape = ShapeOf(n);
		if (!shape.Ok()) return m_Ctx.Fail(&n, QueryResult<Value>::Fail(shape.error, shape.message));
		auto operands = Operands(shape.value);
		if (!operands.Ok()) return QueryResult<Value>::Fail(operands.error, operands.message);
		return Combine(n, operation.value, operands.value, shape.value.fromRight);
	}

	QueryResult<FoldLowerer::Operation> FoldLowerer::OperationOf(const Core::String& op)
	{
		if (op == "&&") return Operation::And;
		if (op == "||") return Operation::Or;
		if (op == ",") return Operation::Comma;
		for (const char* binary : kBinaryFoldOperators)
			if (op == binary) return Operation::Binary;
		if (IsAssignment(op))
			return QueryResult<Operation>::Fail(QueryError::Unsupported,
				"a fold cannot assign: write ((args " + op + " value), ...) to apply '" + op + "' to each element");
		return QueryResult<Operation>::Fail(QueryError::Unsupported,
			"'" + op + "' cannot fold a pack: a fold takes an arithmetic, bitwise, comparison or logical operator, or ','");
	}

	QueryResult<FoldLowerer::Shape> FoldLowerer::ShapeOf(const FoldExpressionNode& n) const
	{
		const AstNode* first = n.GetFirst().Get();
		const AstNode* last = n.GetLast().Get();
		if (!last) return Shape{ first, nullptr, true };
		if (!first) return Shape{ last, nullptr, false };
		const bool firstIsPack = Packs::Names(m_Ctx.model(), first, nullptr);
		const bool lastIsPack = Packs::Names(m_Ctx.model(), last, nullptr);
		if (firstIsPack == lastIsPack)
			return QueryResult<Shape>::Fail(QueryError::NotAValue, firstIsPack
				? "both operands of the fold '" + n.ToString() + "' name a pack: one of them is the initial value"
				: "the fold '" + n.ToString() + "' names no pack");
		return firstIsPack ? Shape{ first, last, true } : Shape{ last, first, false };
	}

	QueryResult<std::vector<Value>> FoldLowerer::Operands(const Shape& shape)
	{
		using Result = QueryResult<std::vector<Value>>;
		std::vector<Value> operands;
		auto add = [&](const AstNode* node) {
			auto v = m_Expr.ResolveExpr(node);
			if (!v.Ok()) return QueryResult<void>::Fail(v.error, v.message);
			operands.push_back(v.value);
			return QueryResult<void>::Success();
		};
		auto copies = PackElements::Of(m_Ctx, *shape.pack);
		if (!copies.Ok()) return Result::Fail(copies.error, copies.message);
		if (shape.initial && !shape.fromRight)
			if (auto r = add(shape.initial); !r.Ok()) return Result::Fail(r.error, r.message);
		if (auto r = copies.value.ForEach([&](uint32) { return add(shape.pack); }); !r.Ok()) return Result::Fail(r.error, r.message);
		if (shape.initial && shape.fromRight)
			if (auto r = add(shape.initial); !r.Ok()) return Result::Fail(r.error, r.message);
		return Result(operands);
	}

	QueryResult<Value> FoldLowerer::Combine(const FoldExpressionNode& n, Operation operation, const std::vector<Value>& operands, bool fromRight)
	{
		if (operands.empty()) return Empty(n, operation);
		if (operation == Operation::Comma) return Sequence(operands);
		Value folded = fromRight ? operands.back() : operands.front();
		for (size_t step = 1; step < operands.size(); ++step)
		{
			auto r = fromRight ? Apply(n, operation, operands[operands.size() - 1 - step], folded) : Apply(n, operation, folded, operands[step]);
			if (!r.Ok()) return r;
			folded = r.value;
		}
		return folded;
	}

	QueryResult<Value> FoldLowerer::Apply(const FoldExpressionNode& n, Operation operation, const Value& lhs, const Value& rhs)
	{
		if (operation == Operation::Binary) return m_Expr.ApplyBinary(&n, n.GetOperatorToken().GetText(), lhs, rhs);
		return m_Expr.ApplyShortCircuit(&n, operation == Operation::And, lhs, rhs);
	}

	QueryResult<Value> FoldLowerer::Empty(const FoldExpressionNode& n, Operation operation)
	{
		// An empty pack: `&&` is true, `||` is false, `,` is no operation; any other operator needs an initial value.
		if (operation == Operation::Comma) return Sequence({});
		if (operation == Operation::Binary)
		{
			const Core::String op = n.GetOperatorToken().GetText();
			return m_Ctx.Fail(&n, QueryResult<Value>::Fail(QueryError::NotAValue, "the fold '" + n.ToString() +
				"' is over an empty pack: give it an initial value, as in (args " + op + " ... " + op + " init)"));
		}
		const Type* boolT = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool);
		return Value::R(m_Ctx.out->NewNode<ConstantNode>(boolT, ConstantValue::Bool(operation == Operation::And)), boolT);
	}

	Value FoldLowerer::Sequence(const std::vector<Value>& operands)
	{
		// Each operand runs in order; the result is the last one, a location when it names one.
		const Type* voidT = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Void);
		if (operands.empty()) return Value::R(m_Ctx.out->NewNode<SequenceNode>(voidT), voidT);
		if (operands.size() == 1) return operands[0];
		Lifecycle life(m_Ctx);
		std::vector<const HIRNode*> steps;
		for (size_t i = 0; i + 1 < operands.size(); ++i) steps.push_back(life.Discarded(operands[i]));
		const Value& last = operands.back();
		if (last.isLValue && HasAddress(last.lvalue))
		{
			const Type* pointer = AddressType(m_Ctx, last.lvalue);
			auto* named = m_Ctx.out->NewNode<AddressOfNode>(pointer, last.lvalue);
			named->transient = true; // the result names the last copy again (LastUseMoves sees through it)
			steps.push_back(named);
			auto* address = m_Ctx.out->NewNode<SequenceNode>(pointer);
			address->steps = std::move(steps);
			LValue location; location.kind = LValue::Kind::Deref; location.type = last.type; location.addr = address;
			location.readOnly = IsReadOnly(m_Ctx, last.lvalue);
			return Value::L(m_Ctx.NewLValue(location));
		}
		const HIRNode* value = m_Ctx.LoadIfNeeded(last);
		steps.push_back(value);
		auto* sequence = m_Ctx.out->NewNode<SequenceNode>(value->type);
		sequence->steps = std::move(steps);
		sequence->fresh = Lifecycle::IsFresh(value);
		return Value::R(sequence, value->type);
	}
} // namespace Scripting
