#include "Backend/HIR/Intrinsics.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/Lifecycle.h"
#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<Value>;

		Result Fail(const Core::String& message) { return Result::Fail(QueryError::NotAValue, message); }

		const Type* VoidType(LoweringContext& ctx) { return ctx.queries().TypeStore()->Builtin(TypeKind::Void); }

		const HIRNode* Operand(LoweringContext& ctx, const Value& v)
		{
			if (!v.isLValue && !v.rvalue) return nullptr; // a braced list
			return ctx.LoadIfNeeded(v);
		}

		const PointerType* AsPointer(const HIRNode* node)
		{
			const Type* t = node ? TypeHandle::UnwrapToValue(node->type) : nullptr;
			return t && t->GetKind() == TypeKind::Pointer ? static_cast<const PointerType*>(t) : nullptr;
		}

		// The object type a pointer operand points to; null for a non-pointer or `void*`.
		const Type* PointeeOf(const HIRNode* pointer)
		{
			const PointerType* p = AsPointer(pointer);
			const Type* pointee = p ? TypeHandle::UnwrapToValue(p->GetPointeeType()) : nullptr;
			return pointee && pointee->GetKind() != TypeKind::Void ? pointee : nullptr;
		}

		const LValue* ObjectAt(LoweringContext& ctx, const HIRNode* pointer, const Type* t)
		{
			LValue lv; lv.kind = LValue::Kind::Deref; lv.type = t; lv.addr = pointer;
			return ctx.NewLValue(lv);
		}

		Result Take(LoweringContext& ctx, const std::vector<Value>& args)
		{
			if (args.size() != 1 || !args[0].isLValue) return Fail("take(x) needs one variable, field or element to move from");
			if (Core::String why = ReadOnlyReason(ctx, args[0].lvalue); !why.empty())
				return Fail("take(x) leaves x zero, but " + why);
			const HIRNode* moved = Lifecycle(ctx).Take(args[0].lvalue);
			return Value::R(moved, moved->type);
		}

		Result Destroy(LoweringContext& ctx, const std::vector<Value>& args)
		{
			const HIRNode* p = args.size() == 1 ? Operand(ctx, args[0]) : nullptr;
			const Type* t = PointeeOf(p);
			if (!t) return Fail("destroy(p) needs one pointer to the object to destroy");
			if (t->GetKind() == TypeKind::Array) return Fail("destroy(p) destroys one object: destroy each element of an array");
			if (const HIRNode* call = Lifecycle(ctx).DestroyCall(ObjectAt(ctx, p, t))) return Value::R(call, VoidType(ctx));
			auto* evaluated = ctx.out->NewNode<SequenceNode>(VoidType(ctx)); // nothing to run; p is still evaluated
			evaluated->steps.push_back(p);
			return Value::R(evaluated, VoidType(ctx));
		}

		Result Relocate(LoweringContext& ctx, const std::vector<Value>& args)
		{
			if (args.size() != 2) return Fail("relocate(dst, src) takes the destination and the source pointer");
			const HIRNode* dst = Operand(ctx, args[0]);
			const HIRNode* src = Operand(ctx, args[1]);
			const Type* t = PointeeOf(dst);
			if (!t || t != PointeeOf(src)) return Fail("relocate(dst, src) needs two pointers to the same type");
			if (AsPointer(dst)->IsConstReferent() || AsPointer(src)->IsConstReferent())
				return Fail("relocate(dst, src) writes both objects: neither pointer may point to const");
			return Value::R(ctx.out->NewNode<RelocateNode>(t, dst, src), VoidType(ctx));
		}
	}

	bool IsLifecycleIntrinsic(const Core::String& name)
	{
		return name == "take" || name == "destroy" || name == "relocate";
	}

	QueryResult<Value> LowerLifecycleIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args)
	{
		if (name == "take") return Take(ctx, args);
		if (name == "destroy") return Destroy(ctx, args);
		return Relocate(ctx, args);
	}
} // namespace Scripting
