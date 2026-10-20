#pragma once
#include "Vector.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "Bivector.h"
#include <cmath>

namespace Math
{
	// ─── Rotor (pure rotation in 3D geometric algebra) ─────────────────────
	// A rotor is an even-grade multivector: scalar + spatial bivector.
	// It encodes a 3D rotation and maps bijectively to a unit quaternion.
	// Layout:   scalar ↔ quaternion w
	//           spatial.x (e12) ↔ quaternion z  (rotation about Z)
	//           spatial.y (e23) ↔ quaternion x  (rotation about X)
	//           spatial.z (e31) ↔ quaternion y  (rotation about Y)
	struct Rotor
	{
		float scalar;            // Even-grade scalar component
		Vector3F spatial;        // Spatial bivector (e12, e23, e31)

		constexpr Rotor() : scalar(1.f), spatial(0.f, 0.f, 0.f) {}
		constexpr Rotor(float p_scalar, const Vector3F& p_spatial)
			: scalar(p_scalar), spatial(p_spatial) {}

		static constexpr Rotor Identity() { return { 1.f, Vector3F(0.f, 0.f, 0.f) }; }

		// ─── Quaternion roundtrip ──────────────────────────────────────
		constexpr Quaternion ToQuaternion() const
		{
			return { spatial.y, spatial.z, spatial.x, scalar };
		}

		static constexpr Rotor FromQuaternion(const Quaternion& q)
		{
			return { q.w, Vector3F(q.z, q.x, q.y) };
		}

		// ─── Matrix conversion ─────────────────────────────────────────
		Mat3 ToMat3() const
		{
			return Mat3Cast(ToQuaternion());
		}

		Mat4 ToMat4() const
		{
			return Mat4Cast(ToQuaternion());
		}

		// ─── Construction from axis-angle ──────────────────────────────
		static Rotor FromAxisAngle(float angle, const Vector3F& axis)
		{
			float halfAngle = angle * 0.5f;
			float s = std::sin(halfAngle);
			Vector3F a = Normalized(axis);

			// axis → bivector: x-axis→e23(spatial.y), y-axis→e31(spatial.z), z-axis→e12(spatial.x)
			return { std::cos(halfAngle), Vector3F(a.z * s, a.x * s, a.y * s) };
		}

		// ─── Rotor multiplication (compose rotations: this ∘ other) ────
		constexpr Rotor operator*(const Rotor& o) const
		{
			// Compose via quaternion multiplication for correctness
			Quaternion q1 = ToQuaternion();
			Quaternion q2 = o.ToQuaternion();
			return FromQuaternion(q1 * q2);
		}

		// ─── Inverse / conjugate (for unit rotor, inverse == conjugate) ─
		constexpr Rotor Inverse() const
		{
			return { scalar, -spatial };
		}

		constexpr Rotor Conjugate() const { return Inverse(); }

		// ─── Normalize ─────────────────────────────────────────────────
		Rotor Normalize() const
		{
			float len = std::sqrt(scalar * scalar + spatial.x * spatial.x + spatial.y * spatial.y + spatial.z * spatial.z);
			if (len < 1e-9f) return Identity();
			float invLen = 1.0f / len;
			return { scalar * invLen, spatial * invLen };
		}

		// ─── Transform a direction vector by this rotor ────────────────
		Vector3F TransformVector(const Vector3F& v) const
		{
			return ToQuaternion().RotateVector(v);
		}

		// ─── Angle and axis extraction ─────────────────────────────────
		float Angle() const
		{
			float c = std::clamp(scalar, -1.0f, 1.0f);
			return 2.0f * std::acos(c);
		}

		Vector3F Axis() const
		{
			float s = std::sqrt(1.0f - scalar * scalar);
			if (s < 1e-6f) return Vector3F(1.f, 0.f, 0.f);
			// Reverse the bivector→axis mapping
			return Vector3F(spatial.y / s, spatial.z / s, spatial.x / s);
		}

		// ─── Scalar multiplication ─────────────────────────────────────
		constexpr Rotor operator*(float s) const { return { scalar * s, spatial * s }; }
	};

	inline constexpr Rotor operator*(float s, const Rotor& r) { return r * s; }

	// ─── Rotor equality ──────────────────────────────────────────────────
	inline constexpr bool operator==(const Rotor& a, const Rotor& b)
	{
		return a.scalar == b.scalar && a.spatial == b.spatial;
	}
	inline constexpr bool operator!=(const Rotor& a, const Rotor& b) { return !(a == b); }

	// ─── Namespace-level rotor utilities ──────────────────────────────────
	inline Rotor Normalize(const Rotor& r) { return r.Normalize(); }


	// ─── Motor (rigid transform in geometric algebra / dual quaternion) ───
	// A motor encodes both rotation and translation. It is the GA equivalent
	// of a unit dual quaternion:  DQ = Qr + ε·Qd
	//   Real part    (w, spatial)       — unit quaternion for rotation
	//   Dual part    (ew, translational) — couples translation with rotation
	//
	// Translation extraction: t = 2 · vec(Qd × conj(Qr))
	// Construction from transform: Qd = (t/2 as pure quat) × Qr
	struct Motor
	{
		float w;               // Real scalar (rotation quaternion w)
		Vector3F spatial;      // Real bivector — rotational part (e12, e23, e31)

		float ew;              // Dual scalar
		Vector3F translational; // Dual bivector — translation-coupled part (e01, e02, e03)

		constexpr Motor() : w(1.f), spatial(0.f, 0.f, 0.f), ew(0.f), translational(0.f, 0.f, 0.f) {}

		constexpr Motor(float p_w, const Vector3F& p_spatial, float p_ew, const Vector3F& p_translational)
			: w(p_w), spatial(p_spatial), ew(p_ew), translational(p_translational) {}

		static constexpr Motor Identity() { return {}; }

		// ─── Construction from rotation + translation ──────────────────
		static Motor FromTransform(const Vector3F& position, const Quaternion& rotation)
		{
			// Real part = rotation quaternion (in rotor layout)
			Rotor r = Rotor::FromQuaternion(rotation);

			// Dual part: Qd = (t/2) × Qr  where t is stored as pure quaternion
			Vector3F halfT = position * 0.5f;
			// Quaternion multiply (halfT as pure quat) × rotation
			// q1 = (hx, hy, hz, 0), q2 = (rx.x, rx.y, rx.z, rw)
			Quaternion tQuat(halfT, 0.f);
			Quaternion dQ = tQuat * rotation;

			return { r.scalar, r.spatial, dQ.w, Vector3F(dQ.z, dQ.x, dQ.y) };
		}

		static Motor FromTransform(const Vector3F& position, const Rotor& rotation)
		{
			return FromTransform(position, rotation.ToQuaternion());
		}

		// ─── Matrix roundtrip ──────────────────────────────────────────
		Mat4 ToMat4() const
		{
			// Extract rotation from real quaternion
			Quaternion q = GetRotation();
			Mat4 result = Mat4Cast(q);

			// Extract translation: t = 2 · vec(Qd × conj(Qr))
			Vector3F t = GetTranslation();
			result.columns[3].x = t.x;
			result.columns[3].y = t.y;
			result.columns[3].z = t.z;

			return result;
		}

		static Motor FromMat4(const Mat4& m)
		{
			// Extract rotation quaternion from upper-left 3×3
			Mat3 rot3{
				Vector3F(m.columns[0].x, m.columns[0].y, m.columns[0].z),
				Vector3F(m.columns[1].x, m.columns[1].y, m.columns[1].z),
				Vector3F(m.columns[2].x, m.columns[2].y, m.columns[2].z)
			};
			Quaternion q = QuatCast(rot3);

			// Extract translation from column 3
			Vector3F t(m.columns[3].x, m.columns[3].y, m.columns[3].z);

			return FromTransform(t, q);
		}

		// ─── Accessor helpers ──────────────────────────────────────────
		constexpr Quaternion GetRotation() const
		{
			return { spatial.y, spatial.z, spatial.x, w };
		}

		Vector3F GetTranslation() const
		{
			Quaternion qr = GetRotation();
			// Dual quaternion in standard layout
			Quaternion qd(translational.y, translational.z, translational.x, ew);
			// t = 2 · vec(qd × conj(qr))
			Quaternion conj = qr.Conjugate();
			Quaternion tQuat = qd * conj;
			return Vector3F(tQuat.x * 2.f, tQuat.y * 2.f, tQuat.z * 2.f);
		}

		Rotor GetRotor() const
		{
			return { w, spatial };
		}

		// ─── Motor multiplication (compose transforms: this ∘ other) ───
		Motor operator*(const Motor& o) const
		{
			// Convert to quaternions for clean dual-quaternion multiplication
			Quaternion qr1 = GetRotation();
			Quaternion qd1(translational.y, translational.z, translational.x, ew);

			Quaternion qr2 = o.GetRotation();
			Quaternion qd2(o.translational.y, o.translational.z, o.translational.x, o.ew);

			// Dual quaternion multiplication: (qr1+ε·qd1)(qr2+ε·qd2) = qr1·qr2 + ε·(qr1·qd2 + qd1·qr2)
			Quaternion newQr = qr1 * qr2;
			Quaternion newQd = qr1 * qd2 + qd1 * qr2;

			// Normalize dual quaternion: normalize real part, then orthogonalize dual
			float lenR = std::sqrt(newQr.x * newQr.x + newQr.y * newQr.y + newQr.z * newQr.z + newQr.w * newQr.w);
			if (lenR < 1e-9f) return Identity();
			float invLenR = 1.0f / lenR;
			newQr.x *= invLenR; newQr.y *= invLenR; newQr.z *= invLenR; newQr.w *= invLenR;

			// Orthogonalize: qd -= qr · dot(qr, qd)
			float dot = newQr.x * newQd.x + newQr.y * newQd.y + newQr.z * newQd.z + newQr.w * newQd.w;
			newQd.x -= newQr.x * dot;
			newQd.y -= newQr.y * dot;
			newQd.z -= newQr.z * dot;
			newQd.w -= newQr.w * dot;

			// Convert back to motor layout
			return { newQr.w, Vector3F(newQr.z, newQr.x, newQr.y),
			         newQd.w, Vector3F(newQd.z, newQd.x, newQd.y) };
		}

		// ─── Inverse motor ─────────────────────────────────────────────
		Motor Inverse() const
		{
			Quaternion qr = GetRotation();
			Quaternion qd(translational.y, translational.z, translational.x, ew);

			// Inverse of dual quaternion: conj(DQ) / |qr|²
			// Since |qr| should be 1 for a valid motor, inverse ≈ conjugate
			Quaternion conjR = qr.Conjugate();
			Quaternion conjD = qd.Conjugate();

			float lenSq = qr.x * qr.x + qr.y * qr.y + qr.z * qr.z + qr.w * qr.w;
			if (lenSq < 1e-9f) return Identity();
			float invLenSq = 1.0f / lenSq;

			conjR.x *= invLenSq; conjR.y *= invLenSq; conjR.z *= invLenSq; conjR.w *= invLenSq;
			conjD.x *= invLenSq; conjD.y *= invLenSq; conjD.z *= invLenSq; conjD.w *= invLenSq;

			return { conjR.w, Vector3F(conjR.z, conjR.x, conjR.y),
			         conjD.w, Vector3F(conjD.z, conjD.x, conjD.y) };
		}

		// ─── Normalize motor ───────────────────────────────────────────
		Motor Normalize() const
		{
			Quaternion qr = GetRotation();
			Quaternion qd(translational.y, translational.z, translational.x, ew);

			float lenR = std::sqrt(qr.x * qr.x + qr.y * qr.y + qr.z * qr.z + qr.w * qr.w);
			if (lenR < 1e-9f) return Identity();
			float invLenR = 1.0f / lenR;
			qr.x *= invLenR; qr.y *= invLenR; qr.z *= invLenR; qr.w *= invLenR;

			// Orthogonalize dual part
			float dot = qr.x * qd.x + qr.y * qd.y + qr.z * qd.z + qr.w * qd.w;
			qd.x -= qr.x * dot; qd.y -= qr.y * dot;
			qd.z -= qr.z * dot; qd.w -= qr.w * dot;

			return { qr.w, Vector3F(qr.z, qr.x, qr.y),
			         qd.w, Vector3F(qd.z, qd.x, qd.y) };
		}

		// ─── Transform operations ──────────────────────────────────────
		Vector3F TransformPoint(const Vector3F& p) const
		{
			return GetRotation().RotateVector(p) + GetTranslation();
		}

		Vector3F TransformDirection(const Vector3F& d) const
		{
			return GetRotation().RotateVector(d);
		}

		// ─── Scalar multiplication ─────────────────────────────────────
		constexpr Motor operator*(float s) const
		{
			return { w * s, spatial * s, ew * s, translational * s };
		}
	};

	inline constexpr Motor operator*(float s, const Motor& m) { return m * s; }

	// ─── Motor equality ──────────────────────────────────────────────────
	inline constexpr bool operator==(const Motor& a, const Motor& b)
	{
		return a.w == b.w && a.spatial == b.spatial &&
		       a.ew == b.ew && a.translational == b.translational;
	}
	inline constexpr bool operator!=(const Motor& a, const Motor& b) { return !(a == b); }

	// ─── Namespace-level motor utilities ──────────────────────────────────
	inline Motor Normalize(const Motor& m) { return m.Normalize(); }


	// ─── Plane (oriented plane in 3D) ────────────────────────────────────
	// Represented by normal vector n and signed distance d from origin.
	// Plane equation: n·x + d = 0, where |n| = 1 for a normalized plane.
	struct Plane
	{
		Vector3F normal;  // Unit normal pointing toward the positive side
		float d;          // Signed distance from origin along normal

		constexpr Plane() : normal(0.f, 0.f, 0.f), d(0.f) {}
		constexpr Plane(const Vector3F& p_normal, float p_d) : normal(p_normal), d(p_d) {}
		constexpr Plane(float nx, float ny, float nz, float pd)
			: normal(nx, ny, nz), d(pd) {}

		static constexpr Plane XY() { return { Vector3F(0.f, 0.f, 1.f), 0.f }; }
		static constexpr Plane YZ() { return { Vector3F(1.f, 0.f, 0.f), 0.f }; }
		static constexpr Plane ZX() { return { Vector3F(0.f, 1.f, 0.f), 0.f }; }

		// ─── Construction from point and normal ────────────────────────
		static Plane FromPointNormal(const Vector3F& point, const Vector3F& n)
		{
			Vector3F nn = Normalized(n);
			return { nn, -Dot(nn, point) };
		}

		// Construction from three points (counter-clockwise winding → outward normal)
		static Plane FromPoints(const Vector3F& p0, const Vector3F& p1, const Vector3F& p2)
		{
			Vector3F n = Normalized(Cross(p1 - p0, p2 - p0));
			return { n, -Dot(n, p0) };
		}

		// ─── Signed distance from point to plane ───────────────────────
		constexpr float Distance(const Vector3F& p) const
		{
			return Dot(normal, p) + d;
		}

		// Closest point on plane to a given point
		Vector3F ClosestPoint(const Vector3F& p) const
		{
			float dist = Distance(p);
			return p - normal * dist;
		}

		// ─── Classification ────────────────────────────────────────────
		constexpr bool IsFrontFacing(const Vector3F& p) const { return Distance(p) > 0.f; }
		constexpr bool IsBackFacing(const Vector3F& p) const  { return Distance(p) < 0.f; }

		// ─── Normalize ─────────────────────────────────────────────────
		Plane Normalize() const
		{
			float len = Length(normal);
			if (len < 1e-9f) return Plane();
			float invLen = 1.0f / len;
			return { normal * invLen, d * invLen };
		}

		// ─── Flip plane orientation ────────────────────────────────────
		constexpr Plane Flipped() const { return { -normal, -d }; }

		// ─── Transform by motor (rotation + translation) ───────────────
		Plane Transform(const Motor& m) const
		{
			Vector3F newNormal = m.TransformDirection(normal);
			// A point on the plane: p = -d * normal (closest point to origin)
			Vector3F p = -d * normal;
			Vector3F newP = m.TransformPoint(p);
			float newD = -Dot(newNormal, newP);
			return { newNormal, newD };
		}

		// ─── Transform by rotor (rotation only) ────────────────────────
		Plane Transform(const Rotor& r) const
		{
			Vector3F newNormal = r.TransformVector(normal);
			// For pure rotation, a point on the plane transforms as p' = R·p
			// d' = -n'·(R·(-d·n)) = d · (n'·(R·n)) ... simpler: just rotate the offset point
			Vector3F p = -d * normal;
			Vector3F newP = r.TransformVector(p);
			float newD = -Dot(newNormal, newP);
			return { newNormal, newD };
		}

		// ─── Transform by quaternion ───────────────────────────────────
		Plane Transform(const Quaternion& q) const
		{
			Vector3F newNormal = q.RotateVector(normal);
			Vector3F p = -d * normal;
			Vector3F newP = q.RotateVector(p);
			float newD = -Dot(newNormal, newP);
			return { newNormal, newD };
		}

		// ─── Transform by matrix ───────────────────────────────────────
		Plane Transform(const Mat4& m) const
		{
			// Transform normal using upper-left 3×3 inverse-transpose
			Mat3 rot3{
				Vector3F(m.columns[0].x, m.columns[0].y, m.columns[0].z),
				Vector3F(m.columns[1].x, m.columns[1].y, m.columns[1].z),
				Vector3F(m.columns[2].x, m.columns[2].y, m.columns[2].z)
			};
			Mat3 invRot = rot3.Inverse();
			Vector3F newNormal = Normalized(invRot.Transpose() * normal);

			// Transform a point on the plane
			Vector3F p = -d * normal;
			Vector3F newP = m * p;
			float newD = -Dot(newNormal, newP);
			return { newNormal, newD };
		}

		// ─── Plane-plane intersection (returns line direction) ─────────
		Vector3F Intersect(const Plane& other, float& outDistA, float& outDistB) const
		{
			Vector3F lineDir = Cross(normal, other.normal);
			float lenSq = Dot(lineDir, lineDir);
			if (lenSq < 1e-12f)
			{
				// Parallel planes
				outDistA = 0.f; outDistB = 0.f;
				return Vector3F(0.f, 0.f, 0.f);
			}

			// Find a point on the intersection line
			float len = std::sqrt(lenSq);
			lineDir /= len;

			// Solve for closest point on intersection to origin
			Vector3F perpA = Cross(lineDir, normal);
			float invLenPerpA = 1.0f / Length(perpA);
			outDistA = -d * invLenPerpA;
			outDistB = -other.d * invLenPerpA;

			return lineDir;
		}

		// ─── Ray-plane intersection ────────────────────────────────────
		// Returns true if ray intersects plane; t is the distance along the ray.
		bool IntersectRay(const Vector3F& origin, const Vector3F& direction, float& outT) const
		{
			float denom = Dot(normal, direction);
			if (std::abs(denom) < 1e-9f)
			{
				// Ray is parallel to plane
				outT = 0.f;
				return false;
			}
			outT = -(Dot(normal, origin) + d) / denom;
			return outT >= 0.f;
		}

		// ─── Arithmetic operators ──────────────────────────────────────
		constexpr Plane operator*(float s) const { return { normal * s, d * s }; }
	};

	inline constexpr Plane operator*(float s, const Plane& p) { return p * s; }

	// ─── Plane equality ──────────────────────────────────────────────────
	inline constexpr bool operator==(const Plane& a, const Plane& b)
	{
		return a.normal == b.normal && a.d == b.d;
	}
	inline constexpr bool operator!=(const Plane& a, const Plane& b) { return !(a == b); }

	// ─── Namespace-level plane utilities ──────────────────────────────────
	inline Plane Normalize(const Plane& p) { return p.Normalize(); }


	// ─── Rotor ↔ Motor conversion helpers ────────────────────────────────
	inline Motor FromRotor(const Rotor& r)
	{
		return { r.scalar, r.spatial, 0.f, Vector3F(0.f, 0.f, 0.f) };
	}

	inline Rotor ToRotor(const Motor& m)
	{
		return { m.w, m.spatial };
	}


	// ─── Slerp for motors (rotational interpolation with translation blend) ──
	inline Motor MotorSlerp(const Motor& a, const Motor& b, float t)
	{
		Quaternion qa = a.GetRotation();
		Quaternion qb = b.GetRotation();

		// Ensure shortest path for rotation
		if (Dot(qa, qb) < 0.f) qb = -qb;

		Quaternion q = Slerp(qa, qb, t);
		Vector3F ta = a.GetTranslation();
		Vector3F tb = b.GetTranslation();
		Vector3F tr = Lerp(ta, tb, t);

		return Motor::FromTransform(tr, q);
	}
}
