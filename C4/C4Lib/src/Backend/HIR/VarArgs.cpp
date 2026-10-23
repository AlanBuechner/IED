#include "Backend/HIR/VarArgs.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Expressions/VaArgExpressionNode.h"

namespace Scripting::VarArgs
{
	namespace
	{
		using Result = QueryResult<Value>;

		Result Fail(const Core::String& message) { return Result::Fail(QueryError::NotAValue, message); }

		bool IsList(const Type* t)
		{
			t = t ? TypeHandle::UnwrapToValue(t) : nullptr;
			if (!t || t->GetKind() != TypeKind::Pointer) return false;
			const Type* pointee = TypeHandle::UnwrapToValue(static_cast<const PointerType*>(t)->GetPointeeType());
			return pointee && pointee->GetKind() == TypeKind::Char8;
		}

		// The address of the va_list variable `v`, or why `v` is not one.
		QueryResult<const HIRNode*> ListAddress(LoweringContext& ctx, const Value& v, const char* form, bool writes)
		{
			using Address = QueryResult<const HIRNode*>;
			if (!v.isLValue || !IsList(v.type)) return Address::Fail(QueryError::NotAValue, Core::String(form) + " needs a va_list variable");
			if (writes)
				if (Core::String why = ReadOnlyReason(ctx, v.lvalue); !why.empty())
					return Address::Fail(QueryError::NotAValue, Core::String(form) + " changes the list, but " + why);
			return Address(ctx.AddressAs(ctx.queries().TypeStore()->Pointer(v.type), v.lvalue));
		}

		const Type* VoidType(LoweringContext& ctx) { return ctx.queries().TypeStore()->Builtin(TypeKind::Void); }

		Result Start(LoweringContext& ctx, const std::vector<Value>& args, bool opens)
		{
			const char* form = opens ? "va_start(ap)" : "va_end(ap)";
			if (args.size() != 1) return Fail(Core::String(form) + " takes the va_list");
			if (opens && !(ctx.fn && ctx.fn->type && ctx.fn->type->IsVariadic()))
				return Fail("va_start(ap) opens the arguments of a variadic function: '" + (ctx.fn ? ctx.fn->name : Core::String("?")) + "' has no '...'");
			auto list = ListAddress(ctx, args[0], form, true);
			if (!list.Ok()) return Result::Fail(list.error, list.message);
			return Value::R(ctx.out->NewNode<VaNode>(VoidType(ctx), opens ? VaNode::Op::Start : VaNode::Op::End, list.value), VoidType(ctx));
		}

		Result Copy(LoweringContext& ctx, const std::vector<Value>& args)
		{
			if (args.size() != 2) return Fail("va_copy(dst, src) takes the destination and the source va_list");
			auto destination = ListAddress(ctx, args[0], "va_copy(dst, src)", true);
			if (!destination.Ok()) return Result::Fail(destination.error, destination.message);
			auto source = ListAddress(ctx, args[1], "va_copy(dst, src)", false);
			if (!source.Ok()) return Result::Fail(source.error, source.message);
			return Value::R(ctx.out->NewNode<VaNode>(VoidType(ctx), VaNode::Op::Copy, destination.value, source.value), VoidType(ctx));
		}

		// Variadic arguments are promoted (float to double, small integers to int32): reading the narrow type
		// would read the wrong bytes.
		Core::String ReadProblem(const Type* t)
		{
			switch (t->GetKind())
			{
			case TypeKind::Void: return "va_arg cannot read a 'void'";
			case TypeKind::Reference: return "va_arg reads a value: read a pointer instead of a reference";
			case TypeKind::Array: return "va_arg cannot read an array: an array argument arrives as a pointer to its first element";
			case TypeKind::Float32: return "va_arg cannot read a 'float': a variadic float arrives as a 'double'";
			case TypeKind::Bool: case TypeKind::Int8: case TypeKind::Int16: case TypeKind::UInt8: case TypeKind::UInt16:
			case TypeKind::Char8: case TypeKind::Char16:
				return "va_arg cannot read a '" + t->GetName() + "': a variadic '" + t->GetName() + "' arrives as an 'int32'";
			default: return "";
			}
		}

		// Its bytes are not a copy of an object with a lifecycle, so a C variadic function can neither take nor give one.
		Core::String LifecycleProblem(LoweringContext& ctx, const Type* t)
		{
			if (!LifecycleFacts::NeedsCopy(ctx.queries(), t) && !LifecycleFacts::NeedsDestroy(ctx.queries(), t)) return "";
			const ObjectType* object = dynamic_cast<const ObjectType*>(t);
			const bool text = object && object->FindMethodByName("CStr");
			return "a '" + QualifiedName(t) + "' copies or destroys itself, and '...' passes only bytes" +
				(text ? Core::String(" (pass its text as .CStr())") : Core::String());
		}

		// A C function (printf's %s) reads a C string where a text view passes its bytes: a pointer and a length.
		Core::String TextViewProblem(const Type* t)
		{
			const ObjectType* object = dynamic_cast<const ObjectType*>(t);
			if (!object || !object->FindMethodByName("Data") || !object->FindMethodByName("ByteLength")) return "";
			return "a '" + QualifiedName(t) + "' is a pointer and a length, not a C string: print it with %.*s, passing (int32)v.ByteLength(), v.Data()";
		}
	}

	bool IsIntrinsic(const Core::String& name)
	{
		return name == "va_start" || name == "va_end" || name == "va_copy";
	}

	QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const Core::String& name, const std::vector<Value>& args)
	{
		if (name == "va_copy") return Copy(ctx, args);
		return Start(ctx, args, name == "va_start");
	}

	QueryResult<Value> LowerArgument(LoweringContext& ctx, ExprLowerer& expr, const VaArgExpressionNode& node)
	{
		if (!node.GetList() || !node.GetArgumentType()) return ctx.Fail(&node, Fail("va_arg(list, T) takes the list and the type to read"));
		auto list = expr.ResolveExpr(node.GetList().Get());
		if (!list.Ok()) return list;
		auto address = ListAddress(ctx, list.value, "va_arg(ap, T)", true);
		if (!address.Ok()) return ctx.Fail(&node, Result::Fail(address.error, address.message));
		auto type = ctx.Fail(&node, ctx.queries().ResolveTypeCtx(*node.GetArgumentType(), ctx.unitId, ctx.scope));
		if (!type.Ok()) return Result::Fail(type.error, type.message);
		const Type* read = TypeHandle::UnwrapToValue(type.value);
		if (type.value->GetKind() == TypeKind::Reference) read = type.value;
		if (Core::String why = ReadProblem(read); !why.empty()) return ctx.Fail(&node, Fail(why));
		if (Core::String why = LifecycleProblem(ctx, read); !why.empty()) return ctx.Fail(&node, Fail("va_arg cannot read this type: " + why));
		return Value::R(ctx.out->NewNode<VaNode>(read, VaNode::Op::Arg, address.value), read);
	}

	QueryResult<void> CheckPassed(LoweringContext& ctx, const Type* type, size_t position, bool toForeign)
	{
		const Type* passed = type ? TypeHandle::UnwrapToValue(type) : nullptr;
		Core::String why = passed ? LifecycleProblem(ctx, passed) : Core::String();
		if (why.empty() && passed && toForeign) why = TextViewProblem(passed);
		if (why.empty()) return QueryResult<void>::Success();
		return QueryResult<void>::Fail(QueryError::NotAValue, "argument " + std::to_string(position + 1) + " cannot pass through '...': " + why);
	}
} // namespace Scripting::VarArgs
