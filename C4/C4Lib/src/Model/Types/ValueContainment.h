#pragma once
#include "Model/QueryResult.h"
#include "Model/Types/ObjectType.h"

namespace Scripting
{
	// A type holds its bases, its fields and their array elements by value. Holding itself that way, directly or
	// through other types, would give it no finite size.
	namespace ValueContainment
	{
		/// Fails, naming each step, when `type` holds itself by value (`'Node.next' holds a 'Node'`).
		QueryResult<void> CheckNotSelfContaining(const ObjectType* type);
	}
} // namespace Scripting
