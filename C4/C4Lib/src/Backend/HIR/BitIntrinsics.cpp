#include "Backend/HIR/BitIntrinsics.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/SimdOperands.h"

namespace Scripting::BitIntrinsics
{
	using namespace SimdOperands;

	namespace
	{
		// What an intrinsic gives: a count of bits (an int32 for an integer, the vector's own type for a vector), or a
		// value of its operands' type.
		enum class Result { Count, Value };

		struct Form
		{
			IntrinsicOp op;
			size_t arity;
			Result result;
		};

		bool Lookup(const Core::String& name, Form& out)
		{
			struct Entry { const char* name; Form form; };
			static const Entry kForms[] = {
				{ "bit_count", { IntrinsicOp::CountOnes, 1, Result::Count } },
				{ "bit_leading_zeros", { IntrinsicOp::LeadingZeros, 1, Result::Count } },
				{ "bit_trailing_zeros", { IntrinsicOp::TrailingZeros, 1, Result::Count } },
				{ "bit_rotate_left", { IntrinsicOp::RotateLeft, 2, Result::Value } },
				{ "bit_rotate_right", { IntrinsicOp::RotateRight, 2, Result::Value } },
				{ "byte_swap", { IntrinsicOp::ByteSwap, 1, Result::Value } },
				{ "mul_high", { IntrinsicOp::MulHigh, 2, Result::Value } },
			};
			for (const Entry& e : kForms)
				if (name == e.name) { out = e.form; return true; }
			return false;
		}

		// The integer type an operand's bits are counted or moved in: its own, or its vector's lanes'.
		const Type* IntegerOf(const Operand& operand) { return operand.lanes ? operand.lanes->GetElementType() : operand.type; }

		Core::String OperandProblem(const Call& call, const Operand& operand)
		{
			if (IsInteger(IntegerOf(operand))) return "";
			return call.name + " works on an integer or a packed integer vector, not a " + Quoted(operand.type);
		}

		// The second operand (a rotation's count, a product's other factor) as the first's type.
		QueryResult<const HIRNode*> Second(const Call& call, const Operand& first)
		{
			const Operand second = Read(call.ctx, call.args[1]);
			if (Core::String why = OperandProblem(call, second); !why.empty()) return QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, why);
			if (second.type == first.type || (second.lanes && second.lanes == first.lanes)) return second.node;
			if (second.lanes) return QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, call.name + "'s operands are a " + Quoted(first.type) +
				" and a " + Quoted(second.type) + ": a vector combines only with a vector of the same lanes or with a scalar");
			auto converted = ImplicitConversion::Scalar(call.ctx, second.node, IntegerOf(first));
			if (!converted.Ok()) return QueryResult<const HIRNode*>::Fail(converted.error, call.name + ": " + converted.message);
			return converted.value;
		}

		const Type* ResultType(const Call& call, const Form& form, const Operand& first)
		{
			if (form.result == Result::Value || first.lanes) return first.lanes ? first.lanes : first.type;
			return call.ctx.queries().TypeStore()->Builtin(TypeKind::Int32);
		}
	}

	bool IsIntrinsic(const Core::String& name)
	{
		Form form;
		return Lookup(name, form);
	}

	QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args)
	{
		Form form;
		Lookup(name, form);
		const Call call{ ctx, name, args };
		if (Core::String why = ArgumentCount(call, form.arity); !why.empty()) return SimdOperands::Fail(why);
		const Operand first = Read(ctx, args[0]);
		if (Core::String why = OperandProblem(call, first); !why.empty()) return SimdOperands::Fail(why);
		const HIRNode* second = nullptr;
		if (form.arity == 2)
		{
			auto converted = Second(call, first);
			if (!converted.Ok()) return QueryResult<Value>::Fail(converted.error, converted.message);
			second = converted.value;
		}
		const Type* type = ResultType(call, form, first);
		return Value::R(ctx.out->NewNode<IntrinsicOpNode>(type, form.op, first.node, second), type);
	}
} // namespace Scripting::BitIntrinsics
