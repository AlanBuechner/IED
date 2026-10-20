#pragma once
#include "Model/QueryResult.h"
#include "Model/Types/ObjectType.h"
#include <vector>

namespace Scripting
{
	class ProgramModel;

	/// The type of a return list `[float, bool]` (D-34): one anonymous struct per list of element types, laid out
	/// like a C struct (fields `_0`, `_1`, ...) and returned by the C ABI rules like any struct. Its values are
	/// reached only by destructuring.
	class ReturnLists
	{
	public:
		/// The struct for `elements`, made once; an element must be a value (no reference, no `void`).
		static QueryResult<const ObjectType*> Of(const ProgramModel& model, const std::vector<const Type*>& elements);
		/// The name of element `index`'s field.
		static Core::String FieldName(uint32 index);
	};
} // namespace Scripting
