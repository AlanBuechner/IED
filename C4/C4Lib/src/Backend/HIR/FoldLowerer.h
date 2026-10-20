#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	class ExprLowerer;
	struct FoldExpressionNode;

	/// Fold expressions (docs/language/08 6). `(args op ...)` is e0 op (e1 op (... op en)) and `(... op args)` is
	/// ((e0 op e1) op ...) op en; a written initial value is the innermost operand. `,` evaluates the copies in
	/// order, and `&&` / `||` stop at the first copy that decides the result.
	class FoldLowerer
	{
	public:
		FoldLowerer(LoweringContext& ctx, ExprLowerer& expr) : m_Ctx(ctx), m_Expr(expr) {}

		QueryResult<Value> Lower(const FoldExpressionNode& n);

	private:
		enum class Operation { Binary, And, Or, Comma };
		/// Which operand is the pack, and which way the fold nests.
		struct Shape
		{
			const AstNode* pack = nullptr;
			const AstNode* initial = nullptr;
			bool fromRight = false;
		};

		static QueryResult<Operation> OperationOf(const Core::String& op);
		QueryResult<Shape> ShapeOf(const FoldExpressionNode& n) const;
		/// The operands in the order they are written: the copies of the pack, with the initial value first
		/// in a left fold and last in a right one.
		QueryResult<std::vector<Value>> Operands(const Shape& shape);
		QueryResult<Value> Combine(const FoldExpressionNode& n, Operation operation, const std::vector<Value>& operands, bool fromRight);
		QueryResult<Value> Apply(const FoldExpressionNode& n, Operation operation, const Value& lhs, const Value& rhs);
		QueryResult<Value> Empty(const FoldExpressionNode& n, Operation operation);
		Value Sequence(const std::vector<Value>& operands);

		LoweringContext& m_Ctx;
		ExprLowerer& m_Expr;
	};
} // namespace Scripting
