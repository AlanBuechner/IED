#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/QueryResult.h"

namespace Scripting
{
	/// Whether `value` may become the pointer `target` (docs/language/04 4.3), for an assignment, a declaration,
	/// an argument or a return: the same pointee, a null constant, `void*` either way, or a derived-to-base
	/// upcast - and never from a pointer to const to a pointer to mutable (D-30). Succeeds when `target` is not
	/// a pointer or `value` is not pointer-like; the message names what is wrong, for the caller to frame.
	QueryResult<void> CheckPointerConversion(const Type* target, const HIRNode* value);
} // namespace Scripting
