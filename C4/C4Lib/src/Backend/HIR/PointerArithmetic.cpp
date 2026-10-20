#include "Backend/HIR/PointerArithmetic.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	namespace
	{
		bool IsPointer(const Type* t) { return t && t->GetKind() == TypeKind::Pointer; }

		const HIRNode* AsInt64(LoweringContext& ctx, const HIRNode* node)
		{
			const Type* int64T = ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
			return node->type == int64T ? node : ctx.out->NewNode<ConvertNode>(node, node->type, int64T);
		}

		const HIRNode* Int64Constant(LoweringContext& ctx, int64 value)
		{
			return ctx.out->NewNode<ConstantNode>(ctx.queries().TypeStore()->Builtin(TypeKind::Int64), ConstantValue::Int(value));
		}

		// The HIR form is an address offset in bytes (AddressArithmeticNode), never an integer add.
		QueryResult<Value> Offset(LoweringContext& ctx, const Core::String& op, const Value& pointer, const Value& count)
		{
			if (!TypeHandle::UnwrapToValue(count.type)->IsIntegral())
				return QueryResult<Value>::Fail(QueryError::NotAValue, "pointer arithmetic needs an integral offset");
			const Type* pointee = TypeHandle::GetPointedType(TypeHandle::UnwrapToValue(pointer.type));
			const int64 elementSize = pointee ? (int64)pointee->GetSize() : 0;
			const Type* int64T = ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
			const HIRNode* bytes = AsInt64(ctx, ctx.LoadIfNeeded(count));
			if (op == "-") bytes = ctx.out->NewNode<IntrinsicOpNode>(int64T, IntrinsicOp::Negate, bytes, nullptr);
			if (elementSize > 1) bytes = ctx.out->NewNode<IntrinsicOpNode>(int64T, IntrinsicOp::Mul, bytes, Int64Constant(ctx, elementSize));
			TypeStore* types = ctx.queries().TypeStore();
			const Type* result = types->Pointer(pointee ? pointee : types->Builtin(TypeKind::Void));
			return Value::R(ctx.out->NewNode<AddressArithmeticNode>(result, ctx.LoadIfNeeded(pointer), bytes), result);
		}

		QueryResult<Value> Difference(LoweringContext& ctx, const Value& lhs, const Value& rhs)
		{
			const Type* left = TypeHandle::GetPointedType(TypeHandle::UnwrapToValue(lhs.type));
			const Type* right = TypeHandle::GetPointedType(TypeHandle::UnwrapToValue(rhs.type));
			const Type* leftValue = TypeHandle::UnwrapToValue(left);
			if (!left || !right || leftValue != TypeHandle::UnwrapToValue(right))
				return QueryResult<Value>::Fail(QueryError::NotAValue, "pointer difference needs two pointers to one type, not '" +
					lhs.type->GetName() + "' and '" + rhs.type->GetName() + "'");
			if (leftValue->GetKind() == TypeKind::Void || leftValue->GetSize() == 0)
				return QueryResult<Value>::Fail(QueryError::NotAValue, "pointer difference needs a pointee with a size: cast the void pointers to char*");
			const Type* int64T = ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
			const HIRNode* bytes = ctx.out->NewNode<IntrinsicOpNode>(int64T, IntrinsicOp::Sub,
				AsInt64(ctx, ctx.LoadIfNeeded(lhs)), AsInt64(ctx, ctx.LoadIfNeeded(rhs)));
			const int64 elementSize = (int64)leftValue->GetSize();
			const HIRNode* elements = elementSize > 1
				? ctx.out->NewNode<IntrinsicOpNode>(int64T, IntrinsicOp::Div, bytes, Int64Constant(ctx, elementSize)) : bytes;
			return Value::R(elements, int64T);
		}
	}

	bool IsPointerArithmetic(const Core::String& op, const Type* left, const Type* right)
	{
		if (op == "+") return IsPointer(left) || IsPointer(right);
		return op == "-" && IsPointer(left);
	}

	QueryResult<Value> PointerArithmetic(LoweringContext& ctx, const Core::String& op, const Value& lhs, const Value& rhs)
	{
		const bool leftPointer = IsPointer(TypeHandle::UnwrapToValue(lhs.type));
		const bool rightPointer = IsPointer(TypeHandle::UnwrapToValue(rhs.type));
		if (op == "-" && leftPointer && rightPointer) return Difference(ctx, lhs, rhs);
		return leftPointer ? Offset(ctx, op, lhs, rhs) : Offset(ctx, op, rhs, lhs);
	}
} // namespace Scripting
