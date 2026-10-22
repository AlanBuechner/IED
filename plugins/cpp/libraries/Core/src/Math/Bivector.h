#pragma once
#include "Vector.h"
#include <cmath>

namespace Math
{
	// --- Bivector ----------------------------------------------------------
	// Grade-2 element of 3D spatial geometric algebra (Cl(3,0) extended with
	// translational bivectors for motor/dual-quaternion use).
	// Represents an oriented plane segment with 6 independent components:
	//   Spatial (rotational) : xy, yz, zx - basis blades e12, e23, e31
	//   Translational        : tx, ty, tz - basis blades e01, e02, e03
	struct Bivector
	{
		Vector3F spatial;       // Rotational plane components (e12, e23, e31)
		Vector3F translational; // Translational components (e01, e02, e03)

		constexpr Bivector() : spatial(0.f, 0.f, 0.f), translational(0.f, 0.f, 0.f) {}

		constexpr Bivector(const Vector3F& p_spatial, const Vector3F& p_translational)
			: spatial(p_spatial), translational(p_translational) {}

		constexpr Bivector(float p_xy, float p_yz, float p_zx, float p_tx, float p_ty, float p_tz)
			: spatial(p_xy, p_yz, p_zx), translational(p_tx, p_ty, p_tz) {}

		static constexpr Bivector Zero() { return {}; }

		// --- Arithmetic operators --------------------------------------
		Bivector operator+(const Bivector& o) const {
			return { spatial + o.spatial, translational + o.translational };
		}
		Bivector operator-(const Bivector& o) const {
			return { spatial - o.spatial, translational - o.translational };
		}
		Bivector operator-() const {
			return { -spatial, -translational };
		}
		Bivector operator*(float s) const {
			return { spatial * s, translational * s };
		}

		// --- Magnitude and normalization -------------------------------
		float Magnitude() const {
			return std::sqrt(spatial.x * spatial.x + spatial.y * spatial.y + spatial.z * spatial.z +
				translational.x * translational.x + translational.y * translational.y + translational.z * translational.z);
		}

		Bivector Normalized() const {
			float m = Magnitude();
			if (m < 1e-9f) return Zero();
			float invM = 1.0f / m;
			return { spatial * invM, translational * invM };
		}

		// --- Inner product (scalar part of geometric product between bivectors) --
		constexpr float Dot(const Bivector& o) const {
			return spatial.x * o.spatial.x + spatial.y * o.spatial.y + spatial.z * o.spatial.z +
			       translational.x * o.translational.x + translational.y * o.translational.y + translational.z * o.translational.z;
		}
	};

	// Scalar * Bivector
	inline constexpr Bivector operator*(float s, const Bivector& b) { return b * s; }

	// --- Equality ----------------------------------------------------------
	inline constexpr bool operator==(const Bivector& a, const Bivector& b) {
		return a.spatial == b.spatial && a.translational == b.translational;
	}
	inline constexpr bool operator!=(const Bivector& a, const Bivector& b) { return !(a == b); }

	// --- Namespace-level utilities -----------------------------------------
	inline float Magnitude(const Bivector& b) { return b.Magnitude(); }
	inline Bivector Normalized(const Bivector& b) { return b.Normalized(); }
}
