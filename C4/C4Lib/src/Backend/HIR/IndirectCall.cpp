#include "Backend/HIR/IndirectCall.h"
#include "Backend/HIR/CallResults.h"
#include "Backend/HIR/CallArguments.h"
#include "Model/Types/SignatureType.h"

namespace Scripting
{
	namespace
	{
		const char* Former(const SignatureType* signature)
		{
			return signature->GetKind() == TypeKind::Func ? "func" : "function pointer";
		}

		QueryResult<const HIRNode*> Argument(LoweringContext& ctx, const SignatureType* fp, const Type* param, const HIRNode* arg, const Value& value, size_t index)
		{
			auto fail = [&](const Core::String& why) {
				return QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, "argument " + std::to_string(index + 1) + " of the " + Former(fp) + " call " + why);
			};
			if (!arg) return fail("cannot be a braced list");
			if (param->GetKind() == TypeKind::Reference) return BindArgument(ctx, value, arg, param);
			auto converted = ConvertedValue(ctx, value.isLValue ? value : Value::R(arg, arg->type), param);
			if (converted.Ok()) return converted;
			const Type* actual = arg->type ? TypeHandle::UnwrapToValue(arg->type) : nullptr;
			return fail("has type '" + (actual ? actual->GetName() : Core::String("?")) + "', expected '" + TypeHandle::UnwrapToValue(param)->GetName() +
				"': " + converted.message);
		}
	}

	QueryResult<Value> LowerIndirectCall(LoweringContext& ctx, const AstNode* site, const HIRNode* target, const SignatureType* fp,
		const std::vector<const HIRNode*>& args, const std::vector<Value>& values)
	{
		if (args.size() != fp->GetParameterCount())
			return ctx.Fail(site, QueryResult<Value>::Fail(QueryError::NotCallable, "the " + Core::String(Former(fp)) + " takes " +
				std::to_string(fp->GetParameterCount()) + " argument(s), got " + std::to_string(args.size())));
		auto* call = ctx.out->NewNode<CallNode>(fp->GetReturnType(), nullptr);
		call->target = target;
		for (size_t i = 0; i < args.size(); ++i)
		{
			auto arg = Argument(ctx, fp, fp->GetParameterTypes()[static_cast<uint32>(i)], args[i], values[i], i);
			if (!arg.Ok()) return ctx.Fail(site, QueryResult<Value>::Fail(arg.error, arg.message));
			call->args.push_back(arg.value);
		}
		return CallResult(ctx, call, fp->GetReturnType());
	}
} // namespace Scripting
