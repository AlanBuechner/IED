#include "Backend/HIR/ConstantOperands.h"

namespace Scripting
{
	namespace
	{
		constexpr int64 kBitsPerByte = 8;

		// A conversion that keeps every value of an integer: to an integer at least as wide.
		bool KeepsValue(const ConvertNode& convert)
		{
			return convert.from && convert.to && convert.from->IsIntegral() && convert.to->IsIntegral() &&
				convert.to->GetSize() >= convert.from->GetSize();
		}

		// `bits` as a value of the integer `type`: its low bits, sign-extended when the type is signed.
		int64 AsIntegerOf(uint64 bits, const Type* type)
		{
			const int64 width = static_cast<int64>(type->GetSize()) * kBitsPerByte;
			if (width >= 64) return static_cast<int64>(bits);
			const uint64 low = bits & ((1ull << width) - 1);
			const uint64 sign = 1ull << (width - 1);
			return type->IsUnsignedInteger() || !(low & sign) ? static_cast<int64>(low) : static_cast<int64>(low | ~((1ull << width) - 1));
		}

		// `-a`, `a + b`, `a - b` and `a * b` of integer constants (`N - 1` in a template), wrapping in the operation's type.
		std::optional<int64> Folded(const IntrinsicOpNode& op)
		{
			if (!op.type || !op.type->IsIntegral() || op.type->GetKind() == TypeKind::Bool) return std::nullopt;
			const std::optional<int64> left = ConstantOperands::IntegerOf(op.lhs);
			if (!left) return std::nullopt;
			const uint64 a = static_cast<uint64>(*left);
			if (op.op == IntrinsicOp::Negate) return AsIntegerOf(0ull - a, op.type);
			const std::optional<int64> right = op.rhs ? ConstantOperands::IntegerOf(op.rhs) : std::nullopt;
			if (!right) return std::nullopt;
			const uint64 b = static_cast<uint64>(*right);
			switch (op.op)
			{
			case IntrinsicOp::Add: return AsIntegerOf(a + b, op.type);
			case IntrinsicOp::Sub: return AsIntegerOf(a - b, op.type);
			case IntrinsicOp::Mul: return AsIntegerOf(a * b, op.type);
			default: return std::nullopt;
			}
		}
	}

	std::optional<int64> ConstantOperands::IntegerOf(const HIRNode* node)
	{
		if (!node) return std::nullopt;
		switch (node->kind)
		{
		case HIRNode::Kind::Constant:
		{
			const ConstantValue& value = static_cast<const ConstantNode*>(node)->value;
			if (value.kind == ConstantValue::Kind::Int64) return value.intValue;
			return std::nullopt;
		}
		case HIRNode::Kind::Convert:
		{
			const auto* convert = static_cast<const ConvertNode*>(node);
			return KeepsValue(*convert) ? IntegerOf(convert->src) : std::nullopt;
		}
		case HIRNode::Kind::IntrinsicOp: return Folded(*static_cast<const IntrinsicOpNode*>(node));
		default:
			return std::nullopt;
		}
	}

	Core::String ConstantOperands::BinaryProblem(IntrinsicOp op, const Type* type, const HIRNode* right)
	{
		if (!type || !type->IsIntegral()) return "";
		const std::optional<int64> value = IntegerOf(right);
		if (!value) return "";
		if ((op == IntrinsicOp::Div || op == IntrinsicOp::Mod) && *value == 0)
			return Core::String(op == IntrinsicOp::Div ? "division" : "remainder") + " by zero: the right operand of '" +
				(op == IntrinsicOp::Div ? "/" : "%") + "' is the constant 0";
		const int64 bits = static_cast<int64>(type->GetSize()) * kBitsPerByte;
		if ((op == IntrinsicOp::Shl || op == IntrinsicOp::Shr) && (*value < 0 || *value >= bits))
			return "the shift count " + std::to_string(*value) + " is outside 0 to " + std::to_string(bits - 1) + " for a '" + type->GetName() + "'";
		return "";
	}

	Core::String ConstantOperands::IndexProblem(const ArrayType& array, const HIRNode* index)
	{
		const std::optional<int64> value = IntegerOf(index);
		if (!value || (*value >= 0 && static_cast<uint64>(*value) < array.GetElementCount())) return "";
		return "the index " + std::to_string(*value) + " is outside the '" + array.GetName() + "' (0 to " +
			std::to_string(array.GetElementCount() - 1) + ")";
	}
} // namespace Scripting
