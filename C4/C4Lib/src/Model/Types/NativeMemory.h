#pragma once
#include "Model/Types/ObjectType.h"

namespace Scripting::NativeMemory
{
	/// The type's own [operator("native_bytes")] method, or null: a base's answers for the base alone.
	const ObjectType::MethodInfo* OwnOperator(const ObjectType* type);

	/// Whether a value of `type` can hold native memory (docs/language/14 section 1): a type with the operator, or one
	/// with a base, a field (not static, not in a union) or array elements that can. Pointers, references and unions
	/// hold none: what a pointer points at is counted on its own.
	bool Holds(const Type* type);

	/// Whether `field` is counted in its object's native memory.
	bool Counted(const ObjectType::FieldInfo& field);
} // namespace Scripting::NativeMemory
