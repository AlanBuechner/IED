#pragma once
#include "Matrix.h"

namespace Math
{
	// --- MatrixFunctions.h ------------------------------------------------
	// Inline transformation functions for Mat4.
	// The core types (Mat2, Mat3, Mat4) and operators live in Matrix.h;
	// this header re-exports them plus provides additional convenience helpers.


	// --- Transform construction functions -----------------------------------
	inline Mat4 Translate(const Vector3<float>& position)
	{
		Mat4 result = Mat4::Identity();
		result.columns[3].x = position.x;
		result.columns[3].y = position.y;
		result.columns[3].z = position.z;
		return result;
	}

	inline Mat4 Scale(const Vector3<float>& scale)
	{
		return Mat4{
			Vector4<float>(scale.x, 0, 0, 0),
			Vector4<float>(0, scale.y, 0, 0),
			Vector4<float>(0, 0, scale.z, 0),
			Vector4<float>(0, 0, 0, 1)
		};
	}

	inline Mat4 Rotate(float angle, const Vector3<float>& axis)
	{
		// Left-handed rotation via Rodrigues' formula (negated cross-product terms vs RH).
		float len = Length(axis);
		if (len == 0.0f) return Mat4::Identity();
		Vector3<float> a = Normalized(axis);
		float s = std::sin(angle);
		float c = std::cos(angle);
		float t = 1.0f - c;

		return Mat4{
			Vector4<float>(t * a.x * a.x + c,     t * a.y * a.x - s * a.z, t * a.z * a.x + s * a.y, 0),
			Vector4<float>(t * a.x * a.y + s * a.z, t * a.y * a.y + c,     t * a.z * a.y - s * a.x, 0),
			Vector4<float>(t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c,     0),
			Vector4<float>(0, 0, 0, 1)
		};
	}

	// RotateX / RotateY / RotateZ are provided by MatrixFunctions.h

	inline Mat4 Ortho(float left, float right, float bottom, float top, float zNear, float zFar)
	{
		float rl = right - left;
		float tb = top - bottom;
		float fn = zFar - zNear;

		return Mat4{
			Vector4<float>(2.0f / rl, 0, 0, 0),
			Vector4<float>(0, 2.0f / tb, 0, 0),
			Vector4<float>(0, 0, 1.0f / fn, 0),
			Vector4<float>(-(right + left) / rl, -(top + bottom) / tb, -zNear / fn, 1)
		};
	}

	inline Mat4 Perspective(float fovy, float aspect, float zNear, float zFar)
	{
		// Left-handed, NDC [0,1] - matches DX12 conventions.
		// Direct column-major layout:
		//   | _00  0    0    0  |     col0 = (_00,  0,    0,  0)
		//   | 0    _11  0    0  |     col1 = (0,    _11,  0,  0)
		//   | 0    0    _22  _32|     col2 = (0,    0,    _22, 1)
		//   | 0    0    1    0  |     col3 = (0,    0,    _32, 0)
		float tanHalfFov = std::tan(fovy * 0.5f);
		float fn = zFar - zNear;

		float _00 = 1.0f / (aspect * tanHalfFov);
		float _11 = 1.0f / tanHalfFov;
		float _22 = zFar / fn;
		float _32 = -(zFar * zNear) / fn;

		return Mat4{
			Vector4<float>(_00, 0,     0, 0),
			Vector4<float>(0,    _11,  0, 0),
			Vector4<float>(0,    0,    _22, 1),
			Vector4<float>(0,    0,    _32, 0)
		};
	}

	inline Mat4 LookAt(const Vector3<float>& eye, const Vector3<float>& center, const Vector3<float>& up)
	{
		// Left-handed look-at
		Vector3<float> z = Normalized(center - eye);
		Vector3<float> x = Normalized(Cross(up, z));
		Vector3<float> y = Cross(z, x);

		return Mat4{
			Vector4<float>(x.x, y.x, z.x, 0),
			Vector4<float>(x.y, y.y, z.y, 0),
			Vector4<float>(x.z, y.z, z.z, 0),
			Vector4<float>(-Dot(x, eye), -Dot(y, eye), -Dot(z, eye), 1)
		};
	}

	// --- Matrix inverse functions (namespace-level for backward compat) -----
	inline Mat2 Inverse(const Mat2& mat) { return mat.Inverse(); }
	inline Mat3 Inverse(const Mat3& mat) { return mat.Inverse(); }
	inline Mat4 Inverse(const Mat4& mat) { return mat.Inverse(); }

	// --- DecomposeTransform ------------------------------------------------
	inline bool DecomposeTransform(const Mat4& transform, Vector3<float>& outPosition, Vector3<float>& outRotation, Vector3<float>& outScale)
	{
		outPosition = Vector3<float>(transform.columns[3].x, transform.columns[3].y, transform.columns[3].z);

		outScale.x = Length(Vector3<float>(transform.columns[0].x, transform.columns[0].y, transform.columns[0].z));
		outScale.y = Length(Vector3<float>(transform.columns[1].x, transform.columns[1].y, transform.columns[1].z));
		outScale.z = Length(Vector3<float>(transform.columns[2].x, transform.columns[2].y, transform.columns[2].z));

		float sx = 1.0f / outScale.x;
		float sy = 1.0f / outScale.y;
		float sz = 1.0f / outScale.z;

		Vector3<float> row0(transform.columns[0].x * sx, transform.columns[0].y * sx, transform.columns[0].z * sx);
		Vector3<float> row1(transform.columns[1].x * sy, transform.columns[1].y * sy, transform.columns[1].z * sy);
		Vector3<float> row2(transform.columns[2].x * sz, transform.columns[2].y * sz, transform.columns[2].z * sz);

		outRotation.y = std::asin(-row0.z);
		if (std::cos(outRotation.y) != 0.0f)
		{
			outRotation.x = std::atan2(row1.z, row2.z);
			outRotation.z = std::atan2(row0.y, row0.x);
		}
		else
		{
			outRotation.x = std::atan2(-row2.x, row1.y);
			outRotation.z = 0.f;
		}

		return true;
	}

	// --- Euler angle helpers ----------------
	inline Mat4 EulerAngleYXZ(float pitch, float yaw, float roll)
	{
		return Rotate(yaw, Vector3(0.f, 1.f, 0.f)) * Rotate(pitch, Vector3(1.f, 0.f, 0.f)) * Rotate(roll, Vector3(0.f, 0.f, 1.f));
	}


	// --- Matrix compound helpers ------------------------------------------
	/// Build a transform from position, rotation (YXZ Euler), and scale.
	inline Mat4 TransformMatrix(const Vector3<float>& position, const Vector3<float>& rotation, const Vector3<float>& scale)
	{
		return Translate(position) * EulerAngleYXZ(rotation.x, rotation.y, rotation.z) * Scale(scale);
	}

	/// Extract the upper-left 3x3 submatrix (rotation + scale).
	inline Mat3 GetRotationScale(const Mat4& m)
	{
		return Mat3{
			Vector3<float>(m.columns[0].x, m.columns[0].y, m.columns[0].z),
			Vector3<float>(m.columns[1].x, m.columns[1].y, m.columns[1].z),
			Vector3<float>(m.columns[2].x, m.columns[2].y, m.columns[2].z)
		};
	}

	/// Extract the translation column.
	inline Vector3<float> GetTranslation(const Mat4& m)
	{
		return Vector3<float>(m.columns[3].x, m.columns[3].y, m.columns[3].z);
	}
}
