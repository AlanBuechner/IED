#pragma once
#include "Vector.h"
#include <cmath>
#include <algorithm>

namespace Math
{
	// ─── Quaternion (w,x,y,z layout) ────────────────────────────────────────
	struct Quaternion
	{
		float x, y, z, w;

		constexpr Quaternion() : x(0), y(0), z(0), w(1) {}
		constexpr Quaternion(float px, float py, float pz, float pw) : x(px), y(py), z(pz), w(pw) {}
		constexpr explicit Quaternion(const Vector3<float>& v, float pw) : x(v.x), y(v.y), z(v.z), w(pw) {}

		// Euler angle constructor matching glm::quat(glm::vec3(pitch, yaw, roll))
		// Rotation order: Yaw(Y) * Pitch(X) * Roll(Z)
		Quaternion(float pitch, float yaw, float roll)
		{
			float halfPitch = pitch * 0.5f;
			float halfYaw = yaw * 0.5f;
			float halfRoll = roll * 0.5f;

			float sp = std::sin(halfPitch), cp = std::cos(halfPitch);
			float sy = std::sin(halfYaw), cy = std::cos(halfYaw);
			float sr = std::sin(halfRoll), cr = std::cos(halfRoll);

			w = cp * cy * cr + sp * sy * sr;
			x = sp * cy * cr - cp * sy * sr;
			y = cp * sy * cr + sp * cy * sr;
			z = cp * cy * sr - sp * sy * cr;
		}

		// Euler angle constructor from Vector3<float>(pitch, yaw, roll)
		Quaternion(const Vector3<float>& eulerAngles)
			: Quaternion(eulerAngles.x, eulerAngles.y, eulerAngles.z) {
		}

		// Identity quaternion
		static constexpr Quaternion Identity() { return { 0, 0, 0, 1 }; }

		// Conjugate
		constexpr Quaternion Conjugate() const { return { -x, -y, -z, w }; }

		// Inverse (for unit quaternion, conjugate == inverse)
		constexpr Quaternion Inverse() const
		{
			float dot = x * x + y * y + z * z + w * w;
			if (dot == 0.0f) return Identity();
			float invDot = 1.0f / dot;
			return { -x * invDot, -y * invDot, -z * invDot, w * invDot };
		}

		// Normalize
		Quaternion Normalize() const
		{
			float len = std::sqrt(x * x + y * y + z * z + w * w);
			if (len == 0.0f) return Identity();
			float invLen = 1.0f / len;
			return { x * invLen, y * invLen, z * invLen, w * invLen };
		}

		// Quaternion multiplication (this * other)
		constexpr Quaternion operator*(const Quaternion& other) const
		{
			return {
				w * other.x + x * other.w + y * other.z - z * other.y,
				w * other.y - x * other.z + y * other.w + z * other.x,
				w * other.z + x * other.y - y * other.x + z * other.w,
				w * other.w - x * other.x - y * other.y - z * other.z
			};
		}

		// Scalar multiplication
		constexpr Quaternion operator*(float s) const { return { x * s, y * s, z * s, w * s }; }

		// Rotate a vector by this quaternion (equivalent to GLM's quat * vec3)
		Vector3<float> operator*(const Vector3<float>& v) const { return RotateVector(v); }

		constexpr Quaternion operator+(const Quaternion& o) const { return { x + o.x, y + o.y, z + o.z, w + o.w }; }
		constexpr Quaternion operator-(const Quaternion& o) const { return { x - o.x, y - o.y, z - o.z, w - o.w }; }
		constexpr Quaternion operator-() const { return { -x, -y, -z, -w }; }

		constexpr bool operator==(const Quaternion& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
		constexpr bool operator!=(const Quaternion& o) const { return !(*this == o); }

		// Rotate a vector by this quaternion
		Vector3<float> RotateVector(const Vector3<float>& v) const
		{
			Vector3<float> qv(x, y, z);
			Vector3<float> cross1 = Cross(qv, v);
			return v + 2.0f * (w * cross1 + Cross(qv, cross1));
		}

		// Raw pointer access for shader uniform uploads
		float* data() { return reinterpret_cast<float*>(this); }
		const float* data() const { return reinterpret_cast<const float*>(this); }
	};

	// Scalar multiplication (s * q)
	inline constexpr Quaternion operator*(float s, const Quaternion& q) { return q * s; }

	// ─── Quaternion functions ──────────────────────────────────────────────
	inline float Dot(const Quaternion& q1, const Quaternion& q2)
	{
		return q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
	}

	inline float Length(const Quaternion& q)
	{
		return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	}

	inline Quaternion Normalize(const Quaternion& q) { return q.Normalize(); }

	inline float Angle(const Quaternion& q)
	{
		// Clamp to [-1, 1] for acos
		float c = std::clamp(q.w, -1.0f, 1.0f);
		return 2.0f * std::acos(c);
	}

	inline Vector3<float> Axis(const Quaternion& q)
	{
		float s = std::sqrt(1.0f - q.w * q.w);
		if (s < 1e-6f) return Vector3<float>(1.f, 0.f, 0.f); // arbitrary axis for identity
		return Vector3<float>(q.x / s, q.y / s, q.z / s);
	}

	inline Quaternion AngleAxis(float angle, const Vector3<float>& axis)
	{
		float halfAngle = angle * 0.5f;
		float s = std::sin(halfAngle);
		Vector3<float> a = Normalized(axis);
		return { a.x * s, a.y * s, a.z * s, std::cos(halfAngle) };
	}

	inline Quaternion AngleAxis(float angle, float px, float py, float pz)
	{
		return AngleAxis(angle, Vector3<float>(px, py, pz));
	}

	inline Quaternion Conjugate(const Quaternion& q) { return q.Conjugate(); }
	inline Quaternion Inverse(const Quaternion& q) { return q.Inverse(); }

	inline Vector3<float> EulerAngles(const Quaternion& q)
	{
		// Build rotation matrix elements from quaternion (row-major access).
		// Mat3Cast stores columns; M[row][col] maps as:
		//   col0 = (M[0][0], M[1][0], M[2][0])
		//   col1 = (M[0][1], M[1][1], M[2][1])
		//   col2 = (M[0][2], M[1][2], M[2][2])
		float m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
		float m01 = 2.0f * (q.x * q.y - q.w * q.z);
		float m02 = 2.0f * (q.x * q.z + q.w * q.y);

		float m10 = 2.0f * (q.x * q.y + q.w * q.z);
		float m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
		float m12 = 2.0f * (q.y * q.z - q.w * q.x);

		float m20 = 2.0f * (q.x * q.z - q.w * q.y);
		float m21 = 2.0f * (q.y * q.z + q.w * q.x);
		float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

		// YXZ Euler extraction (left-handed): M = Ry(yaw) * Rx(pitch) * Rz(roll)
		// pitch = asin(-M[1][2])
		float pitch = std::asin(-m12);

		float cosPitch = std::cos(pitch);
		float yaw, roll;

		if (std::abs(cosPitch) > 1e-6f)
		{
			// Normal case
			yaw = std::atan2(m02, m22);
			roll = std::atan2(m10, m11);
		}
		else
		{
			// Gimbal lock: pitch ≈ ±90°, yaw and roll are degenerate (only yaw-roll difference is determined)
			yaw = std::atan2(m01, m00);
			roll = 0.0f;
		}

		return { pitch, yaw, roll };
	}

	inline float Pitch(const Quaternion& q)
	{
		// YXZ Euler extraction: pitch = asin(-M[1][2])
		float m12 = 2.0f * (q.y * q.z - q.w * q.x);
		return std::asin(-m12);
	}

	inline float Yaw(const Quaternion& q)
	{
		// YXZ Euler extraction: yaw = atan2(M[0][2], M[2][2]) when |cos(pitch)| > epsilon
		float m12 = 2.0f * (q.y * q.z - q.w * q.x);
		float pitch = std::asin(-m12);
		float cosPitch = std::cos(pitch);

		if (std::abs(cosPitch) > 1e-6f)
		{
			float m02 = 2.0f * (q.x * q.z + q.w * q.y);
			float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
			return std::atan2(m02, m22);
		}
		else
		{
			// Gimbal lock: use M[0][1] / M[0][0] to recover yaw-roll difference (with roll = 0)
			float m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
			float m01 = 2.0f * (q.x * q.y - q.w * q.z);
			return std::atan2(m01, m00);
		}
	}

	inline float Roll(const Quaternion& q)
	{
		// YXZ Euler extraction: roll = atan2(M[1][0], M[1][1]) when |cos(pitch)| > epsilon
		float m12 = 2.0f * (q.y * q.z - q.w * q.x);
		float pitch = std::asin(-m12);
		float cosPitch = std::cos(pitch);

		if (std::abs(cosPitch) > 1e-6f)
		{
			float m10 = 2.0f * (q.x * q.y + q.w * q.z);
			float m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
			return std::atan2(m10, m11);
		}
		else
		{
			// Gimbal lock: roll is degenerate
			return 0.0f;
		}
	}

	inline Quaternion Lerp(const Quaternion& q1, const Quaternion& q2, float a)
	{
		// Ensure shortest path
		Quaternion r = q2;
		if (Dot(q1, q2) < 0.0f) r = -q2;

		return (q1 * (1.0f - a) + r * a).Normalize();
	}

	inline Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float a)
	{
		// Ensure shortest path
		Quaternion r = q2;
		float cosOmega = Dot(q1, r);
		if (cosOmega < 0.0f)
		{
			r = -q2;
			cosOmega = -cosOmega;
		}

		// If quaternions are nearly parallel, use lerp
		if (cosOmega > 0.9995f)
		{
			return Lerp(q1, q2, a);
		}

		float theta = std::acos(cosOmega);
		float sinTheta = std::sin(theta);
		float scale0 = std::sin((1.0f - a) * theta) / sinTheta;
		float scale1 = std::sin(a * theta) / sinTheta;

		return q1 * scale0 + r * scale1;
	}

	inline Quaternion Mix(const Quaternion& q1, const Quaternion& q2, float a)
	{
		// Use slerp for short arc interpolation
		return Slerp(q1, q2, a);
	}

	inline Quaternion Abs(const Quaternion& q)
	{
		return q.w < 0.0f ? -q : q;
	}

	inline Quaternion Rotate(const Quaternion& q, float angle, const Vector3<float>& axis)
	{
		Quaternion rot = AngleAxis(angle, axis);
		return rot * q;
	}

	// ─── Quaternion ↔ Matrix conversions ──────────────────────────────────
	inline Mat3 Mat3Cast(const Quaternion& q)
	{
		float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
		float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
		float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

		return Mat3{
		Vector3<float>(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)),
		Vector3<float>(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)),
		Vector3<float>(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy))
		};
	}

	inline Mat4 Mat4Cast(const Quaternion& q)
	{
		Mat3 m3 = Mat3Cast(q);
		return Mat4{
			Vector4<float>(m3.columns[0].x, m3.columns[0].y, m3.columns[0].z, 0.f),
			Vector4<float>(m3.columns[1].x, m3.columns[1].y, m3.columns[1].z, 0.f),
			Vector4<float>(m3.columns[2].x, m3.columns[2].y, m3.columns[2].z, 0.f),
			Vector4<float>(0.f, 0.f, 0.f, 1.f)
		};
	}

	inline Quaternion QuatCast(const Mat3& mat)
	{
		// Schwarz's method — identical to glm::quat_cast regardless of handedness.
		float fourXSquaredMinus1 = mat.columns[0].x - mat.columns[1].y - mat.columns[2].z;
		float fourYSquaredMinus1 = mat.columns[1].y - mat.columns[0].x - mat.columns[2].z;
		float fourZSquaredMinus1 = mat.columns[2].z - mat.columns[0].x - mat.columns[1].y;
		float fourWSquaredMinus1 = mat.columns[0].x + mat.columns[1].y + mat.columns[2].z;

		int biggestIndex = 0;
		float fourBiggestSquaredMinus1 = fourWSquaredMinus1;
		if (fourXSquaredMinus1 > fourBiggestSquaredMinus1)
		{
			fourBiggestSquaredMinus1 = fourXSquaredMinus1;
			biggestIndex = 1;
		}
		if (fourYSquaredMinus1 > fourBiggestSquaredMinus1)
		{
			fourBiggestSquaredMinus1 = fourYSquaredMinus1;
			biggestIndex = 2;
		}
		if (fourZSquaredMinus1 > fourBiggestSquaredMinus1)
		{
			fourBiggestSquaredMinus1 = fourZSquaredMinus1;
			biggestIndex = 3;
		}

		float biggestVal = std::sqrt(fourBiggestSquaredMinus1 + 1.0f) * 0.5f;
		float mult = 0.25f / biggestVal;

		switch (biggestIndex)
		{
		case 0:
			return {
				(mat.columns[1].z - mat.columns[2].y) * mult,
				(mat.columns[2].x - mat.columns[0].z) * mult,
				(mat.columns[0].y - mat.columns[1].x) * mult,
				biggestVal
			};
		case 1:
			return {
				biggestVal,
				(mat.columns[0].y + mat.columns[1].x) * mult,
				(mat.columns[2].x + mat.columns[0].z) * mult,
				(mat.columns[1].z - mat.columns[2].y) * mult
			};
		case 2:
			return {
				(mat.columns[2].x - mat.columns[0].z) * mult,
				(mat.columns[0].y + mat.columns[1].x) * mult,
				biggestVal,
				(mat.columns[1].z + mat.columns[2].y) * mult
			};
		case 3:
			return {
				(mat.columns[2].x + mat.columns[0].z) * mult,
				(mat.columns[1].z + mat.columns[2].y) * mult,
				biggestVal,
				(mat.columns[0].y - mat.columns[1].x) * mult
			};
		default:
			return Quaternion::Identity();
		}
	}

	inline Quaternion QuatCast(const Mat4& mat)
	{
		// Extract upper-left 3×3 submatrix
		Mat3 m3{
			Vector3<float>(mat.columns[0].x, mat.columns[0].y, mat.columns[0].z),
			Vector3<float>(mat.columns[1].x, mat.columns[1].y, mat.columns[1].z),
			Vector3<float>(mat.columns[2].x, mat.columns[2].y, mat.columns[2].z)
		};
		return QuatCast(m3);
	}
}
