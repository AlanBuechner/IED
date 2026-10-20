#include "Backend/HIR/Atomics.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/ConstantOperands.h"
#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"

namespace Scripting::Atomics
{
	namespace
	{
		using Result = QueryResult<Value>;
		using Op = AtomicNode::Op;
		using Order = AtomicNode::Order;

		constexpr int64 kOrderCount = 5; // the members of `MemoryOrder` (the prelude)
		constexpr size_t kLargestAtomicBytes = 8;

		struct Form
		{
			Op op;
			uint32 arguments;   // the pointer, the values, the orders
			bool returnsValue;
			bool integerOnly;
		};

		bool Lookup(const Core::String& name, Form& out)
		{
			struct Entry { const char* name; Form form; };
			static const Entry kForms[] = {
				{ "atomic_load", { Op::Load, 2, true, false } },
				{ "atomic_store", { Op::Store, 3, false, false } },
				{ "atomic_exchange", { Op::Exchange, 3, true, false } },
				{ "atomic_compare_exchange", { Op::CompareExchange, 5, true, false } },
				{ "atomic_compare_exchange_weak", { Op::CompareExchangeWeak, 5, true, false } },
				{ "atomic_fetch_add", { Op::FetchAdd, 3, true, true } },
				{ "atomic_fetch_sub", { Op::FetchSub, 3, true, true } },
				{ "atomic_fetch_and", { Op::FetchAnd, 3, true, true } },
				{ "atomic_fetch_or", { Op::FetchOr, 3, true, true } },
				{ "atomic_fetch_xor", { Op::FetchXor, 3, true, true } },
				{ "atomic_fence", { Op::Fence, 1, false, false } },
			};
			for (const Entry& e : kForms)
				if (name == e.name) { out = e.form; return true; }
			return false;
		}

		Result Fail(const Core::String& message) { return Result::Fail(QueryError::NotAValue, message); }

		const Type* Builtin(LoweringContext& ctx, TypeKind kind) { return ctx.queries().TypeStore()->Builtin(kind); }

		// A value one machine operation holds: a number, a bool, a char, an enum or a pointer of at most 8 bytes.
		bool IsAtomicValue(const Type* t)
		{
			const bool kind = t->IsNumeric() || t->GetKind() == TypeKind::Pointer || t->GetKind() == TypeKind::FunPtr;
			return kind && !t->IsNormalized() && t->GetSize() >= 1 && t->GetSize() <= kLargestAtomicBytes;
		}

		bool IsAtomicInteger(const Type* t) { return t->IsIntegral() && !t->IsNormalized() && t->GetKind() != TypeKind::Bool; }

		// What `p` points at, when it is a pointer to an atomic value the operation may access.
		QueryResult<const Type*> Pointee(const Value& p, const Core::String& form, bool writes)
		{
			using Pointed = QueryResult<const Type*>;
			const Type* t = TypeHandle::UnwrapToValue(p.type);
			const auto* pointer = dynamic_cast<const PointerType*>(t);
			if (!pointer)
				return Pointed::Fail(QueryError::NotAValue, form + " takes a pointer to the value, not a '" + Core::String(t ? t->GetName() : "?") + "'");
			const Type* value = TypeHandle::UnwrapToValue(pointer->GetPointeeType());
			if (!IsAtomicValue(value))
				return Pointed::Fail(QueryError::NotAValue, form + " works on a number, a bool, an enum or a pointer of at most 8 bytes, not a '" +
					value->GetName() + "'");
			if (writes && pointer->IsConstReferent()) return Pointed::Fail(QueryError::NotAValue, form + " writes, but '" + t->GetName() + "' points to const");
			return Pointed(value);
		}

		bool IsMemoryOrder(const Type* t)
		{
			t = t ? TypeHandle::UnwrapToValue(t) : nullptr;
			return t && t->GetKind() == TypeKind::Enum && t->GetName() == "MemoryOrder";
		}

		// A constant order, checked now; or a MemoryOrder only the run time knows (the emitter switches on it).
		struct OrderOperand
		{
			Order constant = Order::SeqCst;
			const HIRNode* runtime = nullptr;
		};

		QueryResult<OrderOperand> OrderOf(LoweringContext& ctx, const Value& v, const Core::String& form)
		{
			using Operand = QueryResult<OrderOperand>;
			const HIRNode* loaded = ctx.LoadIfNeeded(v);
			if (const std::optional<int64> value = ConstantOperands::IntegerOf(loaded))
			{
				if (*value < 0 || *value >= kOrderCount)
					return Operand::Fail(QueryError::NotAValue, form + " takes a MemoryOrder, and " + std::to_string(*value) + " is none of its members");
				return OrderOperand{ static_cast<Order>(*value), nullptr };
			}
			if (!IsMemoryOrder(v.type))
				return Operand::Fail(QueryError::NotAValue, form + " takes its order as a MemoryOrder, such as MemoryOrder::SeqCst");
			return OrderOperand{ Order::SeqCst, loaded };
		}

		const char* Name(Order order)
		{
			switch (order)
			{
			case Order::Relaxed: return "Relaxed";
			case Order::Acquire: return "Acquire";
			case Order::Release: return "Release";
			case Order::AcqRel: return "AcqRel";
			default: return "SeqCst";
			}
		}

		// A load never releases and a store never acquires: the order would have nothing to order. An order known
		// only at run time is not checked: one the operation cannot use acts as SeqCst.
		Core::String OrderProblem(Op op, const OrderOperand& operand, const Core::String& form)
		{
			if (operand.runtime) return "";
			const Order order = operand.constant;
			const Core::String named = form + " cannot use MemoryOrder::" + Name(order);
			if (op == Op::Load && (order == Order::Release || order == Order::AcqRel)) return named + ": a load only acquires (Relaxed, Acquire or SeqCst)";
			if (op == Op::Store && (order == Order::Acquire || order == Order::AcqRel)) return named + ": a store only releases (Relaxed, Release or SeqCst)";
			if (op == Op::Fence && order == Order::Relaxed) return named + ": a relaxed fence orders nothing";
			return "";
		}

		Result Fence(LoweringContext& ctx, const Value& orderArgument, const Core::String& form)
		{
			auto order = OrderOf(ctx, orderArgument, form);
			if (!order.Ok()) return Result::Fail(order.error, order.message);
			if (Core::String why = OrderProblem(Op::Fence, order.value, form); !why.empty()) return Fail(why);
			auto* node = ctx.out->NewNode<AtomicNode>(Builtin(ctx, TypeKind::Void), Op::Fence);
			node->order = order.value.constant;
			node->orderValue = order.value.runtime;
			return Value::R(node, node->type);
		}

		// `atomic_compare_exchange(p, expected, desired, success, failure)`: true when `*p` held `*expected` and now
		// holds `desired`; otherwise `*expected` gets what `*p` held.
		Result CompareExchange(LoweringContext& ctx, const std::vector<Value>& args, const Core::String& form, AtomicNode* node)
		{
			const auto* expected = dynamic_cast<const PointerType*>(TypeHandle::UnwrapToValue(args[1].type));
			if (!expected || TypeHandle::UnwrapToValue(expected->GetPointeeType()) != node->valueType || expected->IsConstReferent())
				return Fail(form + " takes the expected value through a '" + node->valueType->GetName() + "*': it writes the value it found there");
			auto desired = InitialValue(ctx, args[2], node->valueType);
			if (!desired.Ok()) return Result::Fail(desired.error, desired.message);
			auto success = OrderOf(ctx, args[3], form);
			if (!success.Ok()) return Result::Fail(success.error, success.message);
			auto failure = OrderOf(ctx, args[4], form);
			if (!failure.Ok()) return Result::Fail(failure.error, failure.message);
			const Order failed = failure.value.constant;
			if (!failure.value.runtime && (failed == Order::Release || failed == Order::AcqRel))
				return Fail(form + " cannot fail with MemoryOrder::" + Name(failed) + ": a failed exchange only loads");
			node->expected = ctx.LoadIfNeeded(args[1]);
			node->value = desired.value;
			node->order = success.value.constant;
			node->orderValue = success.value.runtime;
			node->failureOrder = failed;
			node->failureOrderValue = failure.value.runtime;
			node->type = Builtin(ctx, TypeKind::Bool);
			return Value::R(node, node->type);
		}

		// `atomic_load(p, order)`, `atomic_store(p, v, order)`, `atomic_exchange`, `atomic_fetch_*`.
		Result Access(LoweringContext& ctx, const Form& form, const std::vector<Value>& args, const Core::String& name, AtomicNode* node)
		{
			auto order = OrderOf(ctx, args.back(), name);
			if (!order.Ok()) return Result::Fail(order.error, order.message);
			if (Core::String why = OrderProblem(form.op, order.value, name); !why.empty()) return Fail(why);
			node->order = order.value.constant;
			node->orderValue = order.value.runtime;
			if (form.op != Op::Load)
			{
				auto operand = InitialValue(ctx, args[1], node->valueType);
				if (!operand.Ok()) return Result::Fail(operand.error, operand.message);
				node->value = operand.value;
			}
			node->type = form.returnsValue ? node->valueType : Builtin(ctx, TypeKind::Void);
			return Value::R(node, node->type);
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
		if (args.size() != form.arguments)
			return Fail(name + " takes " + std::to_string(form.arguments) + " arguments, not " + std::to_string(args.size()));
		if (form.op == Op::Fence) return Fence(ctx, args[0], name);

		auto value = Pointee(args[0], name, form.op != Op::Load);
		if (!value.Ok()) return Result::Fail(value.error, value.message);
		if (form.integerOnly && !IsAtomicInteger(value.value)) return Fail(name + " works on an integer, not a '" + value.value->GetName() + "'");
		auto* node = ctx.out->NewNode<AtomicNode>(value.value, form.op);
		node->valueType = value.value;
		node->address = ctx.LoadIfNeeded(args[0]);
		if (form.op == Op::CompareExchange || form.op == Op::CompareExchangeWeak) return CompareExchange(ctx, args, name, node);
		return Access(ctx, form, args, name, node);
	}
} // namespace Scripting::Atomics
