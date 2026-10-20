#include "Backend/HIR/Callables.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/Types/SignatureType.h"

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<CallableConversion>;

		Result NoConversion() { return Result::Success({}); }

		Result Converted(const HIRNode* node)
		{
			CallableConversion c;
			c.converted = node;
			return Result::Success(c);
		}

		Result Mismatch(const Core::String& why) { return Result::Fail(QueryError::NotAValue, why); }

		bool IsNullConstant(const HIRNode* node)
		{
			const auto* c = dynamic_cast<const ConstantNode*>(node);
			return c && c->type && c->type->GetKind() == TypeKind::Pointer && c->value.AsInt() == 0;
		}

		Core::String DescribeCallable(LoweringContext& ctx, const ObjectType* obj)
		{
			return ctx.model().ClosureOf(obj) ? Core::String("the lambda") : "'" + obj->GetName() + "'";
		}

		// The call method of `obj` with exactly `target`'s signature.
		const FunctionRecord* CallMethod(LoweringContext& ctx, const ObjectType* obj, const SignatureType* target)
		{
			const FunctionRecord* rec = nullptr;
			auto sig = ctx.queries().QueryFunction("operator_call", obj, nullptr, target->GetParameterTypes(), ctx.unitId,
				ctx.enclosingType, ctx.scope, &rec);
			if (!sig.Ok() || !rec) return nullptr;
			Core::Vector<const Type*> params;
			for (const auto& p : sig.value->GetParameters()) params.Push(p.m_Type);
			return target->HasSignature(sig.value->GetReturnType(), params) ? rec : nullptr;
		}

		bool HasCallOperator(const ObjectType* obj)
		{
			for (const auto& m : obj->GetMethods())
				if (m.HasOperator("call")) return true;
			return false;
		}

		const HIRNode* AddressOfCall(LoweringContext& ctx, const FunctionRecord* rec)
		{
			Core::Vector<const Type*> params;
			for (const auto& p : rec->type->GetParameters()) params.Push(p.m_Type);
			const Type* fp = ctx.queries().TypeStore()->FunPtr(rec->type->GetReturnType(), params);
			return ctx.out->NewNode<FunctionAddressNode>(fp, rec);
		}

		// The func gets a GC copy of the object, or for a closure that is one pointer (`[this]`, `[&x]`), the
		// pointer itself - no allocation.
		Result FuncFromObject(LoweringContext& ctx, const Value& value, const ObjectType* obj, const SignatureType* target)
		{
			const FunctionRecord* call = CallMethod(ctx, obj, target);
			if (!call)
			{
				if (!HasCallOperator(obj)) return NoConversion();
				return Mismatch(DescribeCallable(ctx, obj) + " cannot become '" + target->GetName() + "': no call operator of that signature");
			}
			if (auto allowed = AccessCheck::CheckCall(ctx, *call); !allowed.Ok()) return Result::Fail(allowed.error, allowed.message);
			if (call->isStatic)
				return Converted(ctx.out->NewNode<MakeFuncNode>(target, MakeFuncNode::From::Pointer, AddressOfCall(ctx, call)));
			auto object = InitialValue(ctx, value, obj);
			if (!object.Ok()) return Result::Fail(object.error, object.message);
			const Closure* closure = ctx.model().ClosureOf(obj);
			const bool onePointer = closure && closure->IsOnePointer();
			auto* make = ctx.out->NewNode<MakeFuncNode>(target, onePointer ? MakeFuncNode::From::PointerClosure : MakeFuncNode::From::HeapClosure, object.value);
			make->code = call;
			if (!onePointer) make->finalizer = Lifecycle(ctx).Destroy(obj);
			return Converted(make);
		}

		Result ToFunc(LoweringContext& ctx, const Value& value, const SignatureType* target)
		{
			const HIRNode* loaded = ctx.LoadIfNeeded(value);
			if (IsNullConstant(loaded)) return Converted(ctx.out->NewNode<MakeFuncNode>(target, MakeFuncNode::From::Null, nullptr));
			const Type* from = TypeHandle::UnwrapToValue(loaded->type);
			if (const auto* source = dynamic_cast<const SignatureType*>(from))
			{
				if (!target->HasSignature(source->GetReturnType(), source->GetParameterTypes()))
					return Mismatch("cannot convert '" + source->GetName() + "' to '" + target->GetName() + "': the signatures differ");
				if (source->GetKind() == TypeKind::Func) return Converted(loaded);
				return Converted(ctx.out->NewNode<MakeFuncNode>(target, MakeFuncNode::From::Pointer, loaded));
			}
			const auto* obj = dynamic_cast<const ObjectType*>(from);
			return obj ? FuncFromObject(ctx, value, obj, target) : NoConversion();
		}

		Result ToFunPtr(LoweringContext& ctx, const Value& value, const SignatureType* target)
		{
			const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(value.type));
			if (!obj) return NoConversion();
			const FunctionRecord* call = CallMethod(ctx, obj, target);
			if (!call)
			{
				if (!HasCallOperator(obj)) return NoConversion();
				return Mismatch(DescribeCallable(ctx, obj) + " cannot become '" + target->GetName() + "': no call operator of that signature");
			}
			if (auto allowed = AccessCheck::CheckCall(ctx, *call); !allowed.Ok()) return Result::Fail(allowed.error, allowed.message);
			if (!call->isStatic)
				return Mismatch(DescribeCallable(ctx, obj) + " captures variables, so it cannot become a funcptr: store it in a func<...> instead");
			const HIRNode* address = AddressOfCall(ctx, call);
			return Converted(address->type == target ? address : ctx.out->NewNode<ConvertNode>(address, address->type, target));
		}
	}

	QueryResult<CallableConversion> ConvertCallable(LoweringContext& ctx, const Value& value, const SignatureType* target)
	{
		return target->GetKind() == TypeKind::Func ? ToFunc(ctx, value, target) : ToFunPtr(ctx, value, target);
	}

	Value FuncCode(LoweringContext& ctx, const Value& value)
	{
		const Type* t = TypeHandle::UnwrapToValue(value.type);
		if (!t || t->GetKind() != TypeKind::Func) return value;
		TypeStore* ts = ctx.queries().TypeStore();
		const Type* voidPtr = ts->Pointer(ts->Builtin(TypeKind::Void));
		return Value::R(ctx.out->NewNode<FuncCodeNode>(voidPtr, ctx.LoadIfNeeded(value)), voidPtr);
	}
}
