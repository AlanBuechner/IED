#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	class SignatureType;

	/// The implicit conversions of callables (docs/language/07, D-22), the counterpart of OverloadResolution's
	/// callable costs: a func of a signature takes a funcptr, a func, null or an object with a call operator of
	/// that signature (a closure); a funcptr takes an object whose matching call operator is static (a lambda
	/// that captures nothing). `converted` is null when no callable conversion applies, so the caller's own
	/// rules take over; an error is a callable that does not fit.
	struct CallableConversion
	{
		const HIRNode* converted = nullptr;
	};
	QueryResult<CallableConversion> ConvertCallable(LoweringContext& ctx, const Value& value, const SignatureType* target);

	/// A func value's code pointer as a void* (a func is null when its code is); any other value unchanged.
	Value FuncCode(LoweringContext& ctx, const Value& value);
}
