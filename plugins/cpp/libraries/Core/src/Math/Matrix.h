#pragma once
#include "Vector.h"
#include <cmath>
#include <iterator>
#include <type_traits>

namespace Math
{
	// ─── Mat2 (2×2, column-major) ──────────────────────────────────────────
	struct Mat2
	{
		Vector2<float> columns[2];

		constexpr Mat2() = default;
		constexpr explicit Mat2(float diag) : columns{{diag, 0.0f}, {0.0f, diag}} {}
		constexpr Mat2(const Vector2<float>& c0, const Vector2<float>& c1) : columns{c0, c1} {}

		float& operator()(int row, int col) { return columns[col][row]; }
		const float& operator()(int row, int col) const { return columns[col][row]; }
		Vector2<float>& operator[](int col) { return columns[col]; }
		const Vector2<float>& operator[](int col) const { return columns[col]; }

		static constexpr Mat2 Identity() { return Mat2(1.0f); }
		static constexpr Mat2 Zero() { return Mat2{Vector2<float>(0, 0), Vector2<float>(0, 0)}; }

		float Determinant() const
		{
			// GLM parity — column-major 2×2 determinant: m00*m11 - m10*m01
			return columns[0].x * columns[1].y - columns[0].y * columns[1].x;
		}

		constexpr Mat2 Inverse() const
		{
			// GLM parity — column-major 2×2 inverse via adjugate.
			// M = |m00 m01|, columns[0]=(m00,m10), columns[1]=(m01,m11).
			//   |m10 m11|
			// Inverse columns: col0=(+m11/det, -m10/det), col1=(-m01/det, +m00/det)
			float invDet = 1.0f / (columns[0].x * columns[1].y - columns[1].x * columns[0].y);
			return Mat2{
				Vector2<float>(+columns[1].y * invDet, -columns[0].y * invDet),
				Vector2<float>(-columns[1].x * invDet, +columns[0].x * invDet)
			};
		}

		Mat2 Transpose() const
		{
			return Mat2{
				Vector2<float>(columns[0].x, columns[1].x),
				Vector2<float>(columns[0].y, columns[1].y)
			};
		}

		// Raw pointer access for shader uniform uploads (like glm::value_ptr)
		float* data() { return reinterpret_cast<float*>(columns); }
		const float* data() const { return reinterpret_cast<const float*>(columns); }
	};

	// ─── Mat3 (3×3, column-major) ──────────────────────────────────────────
	struct Mat3
	{
		Vector3<float> columns[3];

		constexpr Mat3() = default;
		constexpr explicit Mat3(float diag) : columns{{diag, 0.0f, 0.0f}, {0.0f, diag, 0.0f}, {0.0f, 0.0f, diag}} {}
		constexpr Mat3(const Vector3<float>& c0, const Vector3<float>& c1, const Vector3<float>& c2) : columns{c0, c1, c2} {}

		float& operator()(int row, int col) { return columns[col][row]; }
		const float& operator()(int row, int col) const { return columns[col][row]; }
		Vector3<float>& operator[](int col) { return columns[col]; }
		const Vector3<float>& operator[](int col) const { return columns[col]; }

		static constexpr Mat3 Identity() { return Mat3(1.0f); }
		static constexpr Mat3 Zero() { return Mat3{Vector3<float>(0, 0, 0), Vector3<float>(0, 0, 0), Vector3<float>(0, 0, 0)}; }

		float Determinant() const
		{
			// GLM parity — column-major 3×3 cofactor expansion along column 0
			return columns[0].x * (columns[1].y * columns[2].z - columns[1].z * columns[2].y)
				 - columns[0].y * (columns[1].x * columns[2].z - columns[1].z * columns[2].x)
				 + columns[0].z * (columns[1].x * columns[2].y - columns[1].y * columns[2].x);
		}

		constexpr Mat3 Inverse() const
		{
			// GLM parity — column-major 3×3 inverse via cofactor/adjugate.
			// Each output column i is {C0i, C1i, C2i} * invDet, where Cij is the (i,j) cofactor.
			float invDet = 1.0f / (
				+columns[0].x * (columns[1].y * columns[2].z - columns[2].y * columns[1].z)
				-columns[1].x * (columns[0].y * columns[2].z - columns[2].y * columns[0].z)
				+columns[2].x * (columns[0].y * columns[1].z - columns[1].y * columns[0].z));

			float c00 = +columns[1].y * columns[2].z - columns[2].y * columns[1].z;
			float c01 = -(columns[0].y * columns[2].z - columns[2].y * columns[0].z);
			float c02 = +columns[0].y * columns[1].z - columns[1].y * columns[0].z;
			float c10 = -(columns[1].x * columns[2].z - columns[2].x * columns[1].z);
			float c11 = +columns[0].x * columns[2].z - columns[2].x * columns[0].z;
			float c12 = -(columns[0].x * columns[1].z - columns[1].x * columns[0].z);
			float c20 = +columns[1].x * columns[2].y - columns[2].x * columns[1].y;
			float c21 = -(columns[0].x * columns[2].y - columns[2].x * columns[0].y);
			float c22 = +columns[0].x * columns[1].y - columns[1].x * columns[0].y;

			return Mat3{
				Vector3<float>(c00 * invDet, c01 * invDet, c02 * invDet),
				Vector3<float>(c10 * invDet, c11 * invDet, c12 * invDet),
				Vector3<float>(c20 * invDet, c21 * invDet, c22 * invDet)
			};
		}

		Mat3 Transpose() const
		{
			return Mat3{
				Vector3<float>(columns[0].x, columns[1].x, columns[2].x),
				Vector3<float>(columns[0].y, columns[1].y, columns[2].y),
				Vector3<float>(columns[0].z, columns[1].z, columns[2].z)
			};
		}

		float* data() { return reinterpret_cast<float*>(columns); }
		const float* data() const { return reinterpret_cast<const float*>(columns); }
	};

	// ─── Mat4 (4×4, column-major) ──────────────────────────────────────────
	struct Mat4
	{
		Vector4<float> columns[4];

		constexpr Mat4() = default;
		constexpr explicit Mat4(float diag)
			: columns{{diag, 0.0f, 0.0f, 0.0f}, {0.0f, diag, 0.0f, 0.0f}, {0.0f, 0.0f, diag, 0.0f}, {0.0f, 0.0f, 0.0f, diag}} {}
		constexpr Mat4(const Vector4<float>& c0, const Vector4<float>& c1, const Vector4<float>& c2, const Vector4<float>& c3)
			: columns{c0, c1, c2, c3} {}

		float& operator()(int row, int col) { return columns[col][row]; }
		const float& operator()(int row, int col) const { return columns[col][row]; }
		Vector4<float>& operator[](int col) { return columns[col]; }
		const Vector4<float>& operator[](int col) const { return columns[col]; }

		static constexpr Mat4 Identity() { return Mat4(1.0f); }
		static constexpr Mat4 Zero() { return Mat4{Vector4<float>(0, 0, 0, 0), Vector4<float>(0, 0, 0, 0), Vector4<float>(0, 0, 0, 0), Vector4<float>(0, 0, 0, 0)}; }

		float Determinant() const
		{
			// GLM parity — column-major cofactor expansion along row 0
			float SubFactor00 = columns[2].z * columns[3].w - columns[3].z * columns[2].w;
			float SubFactor01 = columns[2].y * columns[3].w - columns[3].y * columns[2].w;
			float SubFactor02 = columns[2].y * columns[3].z - columns[3].y * columns[2].z;
			float SubFactor03 = columns[2].x * columns[3].w - columns[3].x * columns[2].w;
			float SubFactor04 = columns[2].x * columns[3].z - columns[3].x * columns[2].z;
			float SubFactor05 = columns[2].x * columns[3].y - columns[3].x * columns[2].y;

			float DetCof0 = +(columns[1].y * SubFactor00 - columns[1].z * SubFactor01 + columns[1].w * SubFactor02);
			float DetCof1 = -(columns[1].x * SubFactor00 - columns[1].z * SubFactor03 + columns[1].w * SubFactor04);
			float DetCof2 = +(columns[1].x * SubFactor01 - columns[1].y * SubFactor03 + columns[1].w * SubFactor05);
			float DetCof3 = -(columns[1].x * SubFactor02 - columns[1].y * SubFactor04 + columns[1].z * SubFactor05);

			return columns[0].x * DetCof0 + columns[0].y * DetCof1 + columns[0].z * DetCof2 + columns[0].w * DetCof3;
		}

		constexpr Mat4 Inverse() const
		{
			// GLM parity — column-major 4×4 inverse via cofactor/adjugate
			float Coef00 = columns[2].z * columns[3].w - columns[3].z * columns[2].w;
			float Coef02 = columns[1].z * columns[3].w - columns[3].z * columns[1].w;
			float Coef03 = columns[1].z * columns[2].w - columns[2].z * columns[1].w;

			float Coef04 = columns[2].y * columns[3].w - columns[3].y * columns[2].w;
			float Coef06 = columns[1].y * columns[3].w - columns[3].y * columns[1].w;
			float Coef07 = columns[1].y * columns[2].w - columns[2].y * columns[1].w;

			float Coef08 = columns[2].y * columns[3].z - columns[3].y * columns[2].z;
			float Coef10 = columns[1].y * columns[3].z - columns[3].y * columns[1].z;
			float Coef11 = columns[1].y * columns[2].z - columns[2].y * columns[1].z;

			float Coef12 = columns[2].x * columns[3].w - columns[3].x * columns[2].w;
			float Coef14 = columns[1].x * columns[3].w - columns[3].x * columns[1].w;
			float Coef15 = columns[1].x * columns[2].w - columns[2].x * columns[1].w;

			float Coef16 = columns[2].x * columns[3].z - columns[3].x * columns[2].z;
			float Coef18 = columns[1].x * columns[3].z - columns[3].x * columns[1].z;
			float Coef19 = columns[1].x * columns[2].z - columns[2].x * columns[1].z;

			float Coef20 = columns[2].x * columns[3].y - columns[3].x * columns[2].y;
			float Coef22 = columns[1].x * columns[3].y - columns[3].x * columns[1].y;
			float Coef23 = columns[1].x * columns[2].y - columns[2].x * columns[1].y;

			// GLM builds four intermediate vectors (Inv0..Inv3) then applies sign-alternating
			// patterns to form the adjugate columns. Expanded here without SIMD shuffles.
			Mat4 result;

			// Column 0 = Inv0 * (+1, -1, +1, -1)
			result.columns[0].x = +(columns[1].y * Coef00 - columns[1].z * Coef04 + columns[1].w * Coef08);
			result.columns[0].y = -(columns[0].y * Coef00 - columns[0].z * Coef04 + columns[0].w * Coef08);
			result.columns[0].z = +(columns[0].y * Coef02 - columns[0].z * Coef06 + columns[0].w * Coef10);
			result.columns[0].w = -(columns[0].y * Coef03 - columns[0].z * Coef07 + columns[0].w * Coef11);

			// Column 1 = Inv1 * (-1, +1, -1, +1)
			result.columns[1].x = -(columns[1].x * Coef00 - columns[1].z * Coef12 + columns[1].w * Coef16);
			result.columns[1].y = +(columns[0].x * Coef00 - columns[0].z * Coef12 + columns[0].w * Coef16);
			result.columns[1].z = -(columns[0].x * Coef02 - columns[0].z * Coef14 + columns[0].w * Coef18);
			result.columns[1].w = +(columns[0].x * Coef03 - columns[0].z * Coef15 + columns[0].w * Coef19);

			// Column 2 = Inv2 * (+1, -1, +1, -1)
			result.columns[2].x = +(columns[1].x * Coef04 - columns[1].y * Coef12 + columns[1].w * Coef20);
			result.columns[2].y = -(columns[0].x * Coef04 - columns[0].y * Coef12 + columns[0].w * Coef20);
			result.columns[2].z = +(columns[0].x * Coef06 - columns[0].y * Coef14 + columns[0].w * Coef22);
			result.columns[2].w = -(columns[0].x * Coef07 - columns[0].y * Coef15 + columns[0].w * Coef23);

			// Column 3 = Inv3 * (-1, +1, -1, +1)
			result.columns[3].x = -(columns[1].x * Coef08 - columns[1].y * Coef16 + columns[1].z * Coef20);
			result.columns[3].y = +(columns[0].x * Coef08 - columns[0].y * Coef16 + columns[0].z * Coef20);
			result.columns[3].z = -(columns[0].x * Coef10 - columns[0].y * Coef18 + columns[0].z * Coef22);
			result.columns[3].w = +(columns[0].x * Coef11 - columns[0].y * Coef19 + columns[0].z * Coef23);

			// Compute determinant: dot(original row 0, adjugate row 0)
			float det = columns[0].x * result.columns[0].x
			          + columns[0].y * result.columns[1].x
			          + columns[0].z * result.columns[2].x
			          + columns[0].w * result.columns[3].x;

			if (det == 0.0f) return Zero();
			float invDet = 1.0f / det;

			return Mat4{
				Vector4<float>(result.columns[0].x * invDet, result.columns[0].y * invDet, result.columns[0].z * invDet, result.columns[0].w * invDet),
				Vector4<float>(result.columns[1].x * invDet, result.columns[1].y * invDet, result.columns[1].z * invDet, result.columns[1].w * invDet),
				Vector4<float>(result.columns[2].x * invDet, result.columns[2].y * invDet, result.columns[2].z * invDet, result.columns[2].w * invDet),
				Vector4<float>(result.columns[3].x * invDet, result.columns[3].y * invDet, result.columns[3].z * invDet, result.columns[3].w * invDet)
			};
		}

		Mat4 Transpose() const
		{
			return Mat4{
				Vector4(columns[0].x, columns[1].x, columns[2].x, columns[3].x),
				Vector4(columns[0].y, columns[1].y, columns[2].y, columns[3].y),
				Vector4(columns[0].z, columns[1].z, columns[2].z, columns[3].z),
				Vector4(columns[0].w, columns[1].w, columns[2].w, columns[3].w)
			};
		}

		// Raw pointer access for shader uniform uploads (like glm::value_ptr)
		float* data() { return reinterpret_cast<float*>(columns); }
		const float* data() const { return reinterpret_cast<const float*>(columns); }
	};

	// ─── Matrix multiplication operators ────────────────────────────────────
	// Matrix × Matrix multiplication
	inline constexpr Mat2 operator*(const Mat2& a, const Mat2& b)
	{
		return Mat2{
			Vector2(a.columns[0].x * b.columns[0].x + a.columns[1].x * b.columns[0].y,
					a.columns[0].y * b.columns[0].x + a.columns[1].y * b.columns[0].y),
			Vector2(a.columns[0].x * b.columns[1].x + a.columns[1].x * b.columns[1].y,
					a.columns[0].y * b.columns[1].x + a.columns[1].y * b.columns[1].y)
		};
	}

	inline constexpr Mat3 operator*(const Mat3& a, const Mat3& b)
	{
		return Mat3{
			Vector3(a.columns[0].x * b.columns[0].x + a.columns[1].x * b.columns[0].y + a.columns[2].x * b.columns[0].z,
					a.columns[0].y * b.columns[0].x + a.columns[1].y * b.columns[0].y + a.columns[2].y * b.columns[0].z,
					a.columns[0].z * b.columns[0].x + a.columns[1].z * b.columns[0].y + a.columns[2].z * b.columns[0].z),
			Vector3(a.columns[0].x * b.columns[1].x + a.columns[1].x * b.columns[1].y + a.columns[2].x * b.columns[1].z,
					a.columns[0].y * b.columns[1].x + a.columns[1].y * b.columns[1].y + a.columns[2].y * b.columns[1].z,
					a.columns[0].z * b.columns[1].x + a.columns[1].z * b.columns[1].y + a.columns[2].z * b.columns[1].z),
			Vector3(a.columns[0].x * b.columns[2].x + a.columns[1].x * b.columns[2].y + a.columns[2].x * b.columns[2].z,
					a.columns[0].y * b.columns[2].x + a.columns[1].y * b.columns[2].y + a.columns[2].y * b.columns[2].z,
					a.columns[0].z * b.columns[2].x + a.columns[1].z * b.columns[2].y + a.columns[2].z * b.columns[2].z)
		};
	}

	inline constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
	{
		return Mat4{
			Vector4(a.columns[0].x * b.columns[0].x + a.columns[1].x * b.columns[0].y + a.columns[2].x * b.columns[0].z + a.columns[3].x * b.columns[0].w,
					a.columns[0].y * b.columns[0].x + a.columns[1].y * b.columns[0].y + a.columns[2].y * b.columns[0].z + a.columns[3].y * b.columns[0].w,
					a.columns[0].z * b.columns[0].x + a.columns[1].z * b.columns[0].y + a.columns[2].z * b.columns[0].z + a.columns[3].z * b.columns[0].w,
					a.columns[0].w * b.columns[0].x + a.columns[1].w * b.columns[0].y + a.columns[2].w * b.columns[0].z + a.columns[3].w * b.columns[0].w),
			Vector4(a.columns[0].x * b.columns[1].x + a.columns[1].x * b.columns[1].y + a.columns[2].x * b.columns[1].z + a.columns[3].x * b.columns[1].w,
					a.columns[0].y * b.columns[1].x + a.columns[1].y * b.columns[1].y + a.columns[2].y * b.columns[1].z + a.columns[3].y * b.columns[1].w,
					a.columns[0].z * b.columns[1].x + a.columns[1].z * b.columns[1].y + a.columns[2].z * b.columns[1].z + a.columns[3].z * b.columns[1].w,
					a.columns[0].w * b.columns[1].x + a.columns[1].w * b.columns[1].y + a.columns[2].w * b.columns[1].z + a.columns[3].w * b.columns[1].w),
			Vector4(a.columns[0].x * b.columns[2].x + a.columns[1].x * b.columns[2].y + a.columns[2].x * b.columns[2].z + a.columns[3].x * b.columns[2].w,
					a.columns[0].y * b.columns[2].x + a.columns[1].y * b.columns[2].y + a.columns[2].y * b.columns[2].z + a.columns[3].y * b.columns[2].w,
					a.columns[0].z * b.columns[2].x + a.columns[1].z * b.columns[2].y + a.columns[2].z * b.columns[2].z + a.columns[3].z * b.columns[2].w,
					a.columns[0].w * b.columns[2].x + a.columns[1].w * b.columns[2].y + a.columns[2].w * b.columns[2].z + a.columns[3].w * b.columns[2].w),
			Vector4(a.columns[0].x * b.columns[3].x + a.columns[1].x * b.columns[3].y + a.columns[2].x * b.columns[3].z + a.columns[3].x * b.columns[3].w,
					a.columns[0].y * b.columns[3].x + a.columns[1].y * b.columns[3].y + a.columns[2].y * b.columns[3].z + a.columns[3].y * b.columns[3].w,
					a.columns[0].z * b.columns[3].x + a.columns[1].z * b.columns[3].y + a.columns[2].z * b.columns[3].z + a.columns[3].z * b.columns[3].w,
					a.columns[0].w * b.columns[3].x + a.columns[1].w * b.columns[3].y + a.columns[2].w * b.columns[3].z + a.columns[3].w * b.columns[3].w)
		};
	}

	// ─── Matrix × scalar multiplication ──────────────────────────────────────
	template<typename MatT, typename ScalarT>
	inline constexpr auto operator*(const MatT& m, ScalarT s) -> MatT
	{
		MatT result;
		for (size_t i = 0; i < std::size(m.columns); ++i)
			result.columns[i] = m.columns[i] * s;
		return result;
	}

	// Matrix × Vector multiplication
	inline constexpr Vector3<float> operator*(const Mat3& m, const Vector3<float>& v)
	{
		return Vector3<float>(
			m.columns[0].x * v.x + m.columns[1].x * v.y + m.columns[2].x * v.z,
			m.columns[0].y * v.x + m.columns[1].y * v.y + m.columns[2].y * v.z,
			m.columns[0].z * v.x + m.columns[1].z * v.y + m.columns[2].z * v.z
		);
	}

	inline constexpr Vector4<float> operator*(const Mat4& m, const Vector4<float>& v)
	{
		return Vector4<float>(
			m.columns[0].x * v.x + m.columns[1].x * v.y + m.columns[2].x * v.z + m.columns[3].x * v.w,
			m.columns[0].y * v.x + m.columns[1].y * v.y + m.columns[2].y * v.z + m.columns[3].y * v.w,
			m.columns[0].z * v.x + m.columns[1].z * v.y + m.columns[2].z * v.z + m.columns[3].z * v.w,
			m.columns[0].w * v.x + m.columns[1].w * v.y + m.columns[2].w * v.z + m.columns[3].w * v.w
		);
	}

	// Mat4 × Vector3 (position transform with translation)
	inline constexpr Vector3<float> operator*(const Mat4& m, const Vector3<float>& v)
	{
		return Vector3<float>(
			m.columns[0].x * v.x + m.columns[1].x * v.y + m.columns[2].x * v.z + m.columns[3].x,
			m.columns[0].y * v.x + m.columns[1].y * v.y + m.columns[2].y * v.z + m.columns[3].y,
			m.columns[0].z * v.x + m.columns[1].z * v.y + m.columns[2].z * v.z + m.columns[3].z
		);
	}

	// ─── Matrix addition/subtraction ────────────────────────────────────────
	inline constexpr Mat4 operator+(const Mat4& a, const Mat4& b)
	{
		return Mat4{
			a.columns[0] + b.columns[0],
			a.columns[1] + b.columns[1],
			a.columns[2] + b.columns[2],
			a.columns[3] + b.columns[3]
		};
	}

	inline constexpr Mat4 operator-(const Mat4& a, const Mat4& b)
	{
		return Mat4{
			a.columns[0] - b.columns[0],
			a.columns[1] - b.columns[1],
			a.columns[2] - b.columns[2],
			a.columns[3] - b.columns[3]
		};
	}

	// ─── Matrix equality ────────────────────────────────────────────────────
	inline constexpr bool operator==(const Mat2& a, const Mat2& b)
	{ return a.columns[0] == b.columns[0] && a.columns[1] == b.columns[1]; }

	inline constexpr bool operator!=(const Mat2& a, const Mat2& b) { return !(a == b); }

	inline constexpr bool operator==(const Mat3& a, const Mat3& b)
	{ return a.columns[0] == b.columns[0] && a.columns[1] == b.columns[1] && a.columns[2] == b.columns[2]; }

	inline constexpr bool operator!=(const Mat3& a, const Mat3& b) { return !(a == b); }

	inline constexpr bool operator==(const Mat4& a, const Mat4& b)
	{
		return a.columns[0] == b.columns[0] && a.columns[1] == b.columns[1] &&
			a.columns[2] == b.columns[2] && a.columns[3] == b.columns[3];
	}

	inline constexpr bool operator!=(const Mat4& a, const Mat4& b) { return !(a == b); }

	// ─── value_ptr helper ────────────────────────
	inline const float* ValuePtr(const Mat2& m) { return reinterpret_cast<const float*>(m.columns); }
	inline float* ValuePtr(Mat2& m) { return reinterpret_cast<float*>(m.columns); }
	inline const float* ValuePtr(const Mat3& m) { return reinterpret_cast<const float*>(m.columns); }
	inline float* ValuePtr(Mat3& m) { return reinterpret_cast<float*>(m.columns); }
	inline const float* ValuePtr(const Mat4& m) { return reinterpret_cast<const float*>(m.columns); }
	inline float* ValuePtr(Mat4& m) { return reinterpret_cast<float*>(m.columns); }
	inline const float* ValuePtr(const Vector2<float>& v) { return &v.x; }
	inline float* ValuePtr(Vector2<float>& v) { return &v.x; }
	inline const float* ValuePtr(const Vector3<float>& v) { return &v.x; }
	inline float* ValuePtr(Vector3<float>& v) { return &v.x; }
	inline const float* ValuePtr(const Vector4<float>& v) { return &v.x; }
	inline float* ValuePtr(Vector4<float>& v) { return &v.x; }

	// ─── epsilonEqual / epsilonNotEqual ────────
	template <typename T>
	inline bool EpsilonEqual(T a, T b, T eps = static_cast<T>(1e-6))
	{
		return std::abs(a - b) <= eps;
	}

	template <typename T>
	inline bool EpsilonNotEqual(T a, T b, T eps = static_cast<T>(1e-6))
	{
		return !EpsilonEqual(a, b, eps);
	}
}
