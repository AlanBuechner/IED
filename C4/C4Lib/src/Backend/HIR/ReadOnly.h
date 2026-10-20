#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// Why `location` cannot be written (docs/language/04 4.2, D-30, D-31), or empty when it can: a const
	/// variable or parameter, a location reached through a pointer or reference to const, `this` in a const
	/// method (a lambda's call operator unless `mutable`), a const field. The object a create, copy or delete
	/// builds or ends is writable there, its const fields included.
	Core::String ReadOnlyReason(const LoweringContext& ctx, const LValue* location);

	inline bool IsReadOnly(const LoweringContext& ctx, const LValue* location) { return !ReadOnlyReason(ctx, location).empty(); }

	/// The type of `&location`: a pointer to const when the location is read-only.
	const Type* AddressType(LoweringContext& ctx, const LValue* location);
} // namespace Scripting
