#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A primitive (builtin) type: void, bool, intN, uintN, snorm/unormN, float, double, char, string.
	/// Size/alignment are fixed per kind and set in the constructor; there is no per-instance data.
	class PrimitiveType : public Type
	{
	public:
		explicit PrimitiveType(TypeKind kind);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }
	};
} // namespace Scripting
