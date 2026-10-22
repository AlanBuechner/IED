#pragma once
#include "Core/Types.h"
#include "Vector.h"
#include "Matrix.h"
#include "Quaternion.h"

namespace Math
{
	// --- Backward-compatible type aliases (match previous GLM-based names) --
	// NOTE: Vector2/3/4 are templates; use Vector2F/3F/4F for float, IVector2F etc. for int, UVector2F for uint.
	// Do NOT alias Vector2 = Vector2F here - it would shadow the Vector2<T> template.

	using IVector2 = IVector2F;
	using IVector3 = IVector3F;
	using IVector4 = IVector4F;

	using UVector2 = UVector2F;
	using UVector3 = UVector3F;
	using UVector4 = UVector4F;

	// Matrix aliases (Mat2, Mat3, Mat4 are already defined in Matrix.h)
	using Mat2x2 = Mat2;
	using Mat3x3 = Mat3;
	using Mat4x4 = Mat4;

	// Quaternion alias (Quaternion is already defined in Quaternion.h)
}
