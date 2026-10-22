#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Compiler/ErrorTracker.h"
#include <vector>

namespace Scripting
{
	class ProgramModel;

	/// The native memory a value holds (docs/language/14 section 1): what the [operator("native_bytes")] methods of its
	/// type, its bases, its fields and the elements of its fixed arrays count (NativeMemory::Holds says which can hold
	/// any). A type with the operator answers for its whole object.
	namespace NativeBytes
	{
		bool IsIntrinsic(const Core::String& name);
		/// `native_bytes_of(x)` of a variable, field, element or `*p`, and `native_bytes_of(p, count)` of the `count`
		/// elements a pointer `p` points to: an int64. The operands are evaluated once, whatever their type.
		QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const std::vector<Value>& args);

		/// The census's function `int64(void* object)` for the objects `new` makes of `type`, whose record is
		/// `function`: the native bytes of the object at `object`, or with `countBytes`, of the counted array there (its
		/// element count first, as an int64, and the elements `countBytes` after it).
		HIRFunction LowerCensusFunction(ProgramModel& model, ErrorTracker& errors, const Type* type, uint64 countBytes,
			const FunctionRecord& function);
	}
} // namespace Scripting
