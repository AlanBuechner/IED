#pragma once
#include "Core/Core.h"

namespace Scripting
{
	class Type;

	/// How a value of `from` becomes the object `to` when nothing converts it by itself (D-32), to end an error with:
	/// `: convert it with (T)x` for a cast operator of `from`, `: pass x.View()` for a public method of `from` that
	/// takes nothing and gives a `to`, `: construct one with T(...)` for a create of `to` that takes a `from`; empty
	/// when `to` is no object or there is no such way.
	Core::String ConversionAdvice(const Type* from, const Type* to);
}
