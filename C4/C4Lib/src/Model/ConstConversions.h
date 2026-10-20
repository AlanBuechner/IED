#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"

namespace Scripting
{
	class Type;

	/// `value` converted to `target` as a cast converts it at run time: an integer wraps to the target's width (a
	/// signed target sign-extends), a float truncates toward zero (one outside the target's range is not a constant),
	/// a `float` rounds to single precision, a `bool` is `value != 0`, an enum converts as its underlying type does.
	/// A pointer, a normalized value or an object is not a constant target.
	QueryResult<ConstantValue> ConvertConstant(const ConstantValue& value, const Type* target);

	/// The value a variable of `target` starts holding when its initializer is the constant `value`: converted as
	/// ConvertConstant converts it, or for a pointer, null (0) or a string's address. Any other start is not constant.
	QueryResult<ConstantValue> StoredConstant(const ConstantValue& value, const Type* target);
}
